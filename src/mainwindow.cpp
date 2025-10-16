#include "mainwindow.h"
#include "dbmanager.h"
#include <QFileDialog>
#include <QStatusBar>
#include <QMenu>
#include <QClipboard>
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QDir>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QLineEdit>
#include <QLabel>
#include <QRegularExpression>
#include <QTextDocument>
#include <QThread>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), m_db(this)
{
    ui.setupUi(this);
    setupResultsTable();

    m_db.open("index.db");

    ui.tableWidgetResults->setContextMenuPolicy(Qt::CustomContextMenu); //меню ПКМ
    statusBar()->showMessage(QString::fromUtf8("Готово"));

    // создаём поток
    m_thread = new QThread(this);
    m_dbWorker = new DBManager();
    m_dbWorker->moveToThread(m_thread);
    connect(m_thread, &QThread::started, m_dbWorker, [this]() {
        m_dbWorker->open("index.db"); // открываем БД именно в потоке
        });

    // индексатор
    m_indexer = new FileIndexer(m_dbWorker);
    m_indexer->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_indexer, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_dbWorker, &QObject::deleteLater);

    // сигнал запуска сканирования
    connect(this, &MainWindow::startScan, m_indexer,
        [this](const QString& dir) {
            m_indexer->scanDirectory(dir, { "*.txt","*.log","*.csv" }, "UTF-8");
        });

    // прогресс — обратно в GUI
    connect(m_indexer, &FileIndexer::scanStarted, this, [this](int total) {
        ui.progressBar->setRange(0, total);
        ui.progressBar->setValue(0);
        ui.progressBar->setVisible(true);
        }, Qt::QueuedConnection);

    connect(m_indexer, &FileIndexer::progressChanged, this, [this](int current, int total) {
        ui.progressBar->setValue(current);
        const int percent = (total > 0) ? (current * 100 / total) : 0;
        ui.progressBar->setFormat(QString("%1%").arg(percent));
        }, Qt::QueuedConnection);

    connect(m_indexer, &FileIndexer::scanFinished, this, [this]() {
        ui.progressBar->setVisible(false);
        statusBar()->showMessage(QString::fromUtf8("Индексирование завершено"));
        }, Qt::QueuedConnection);

    m_thread->start();
}

MainWindow::~MainWindow() {
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
}

//настройка таблицы результата
void MainWindow::setupResultsTable() {
    auto* t = ui.tableWidgetResults;
    t->clear();
    t->setColumnCount(5);
    t->setHorizontalHeaderLabels({
        QString::fromUtf8("Файл"),
        QString::fromUtf8("Строка"),
        QString::fromUtf8("Текст строки"),
        QString::fromUtf8("Дата изм."),
        QString::fromUtf8("Размер")
        });
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->horizontalHeader()->setStretchLastSection(true);
}

//цветное выделение совпадения 
void MainWindow::setHighlightedText(QTableWidget* table, int row, int col,
    const QString& text,
    const QString& query,
    Qt::CaseSensitivity cs)
{
    if (!table || text.isEmpty()) {
        table->setItem(row, col, new QTableWidgetItem(text));
        return;
    }

    // экранируем HTML и подкрашиваем все вхождения
    QString html = text.toHtmlEscaped();
    QRegularExpression re("(" + QRegularExpression::escape(query) + ")", // регулярка на ВСЕ вхождения слова
        (cs == Qt::CaseSensitive)
        ? QRegularExpression::UseUnicodePropertiesOption
        : QRegularExpression::UseUnicodePropertiesOption
        | QRegularExpression::CaseInsensitiveOption);

    html.replace(re, "<span style='background-color: yellow; color: black;'><b>\\1</b></span>");

    auto* lbl = new QLabel; //замена совпадения на QLabel для вставки HTML текста
    lbl->setTextFormat(Qt::RichText);
    lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lbl->setWordWrap(true);
    lbl->setText(html);

    table->setCellWidget(row, col, lbl);
}

//выбор папки по кнопке Обзор
void MainWindow::on_pushButtonBrowse_clicked() {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Выберите папку для индексации"));
    if (!dir.isEmpty())
        ui.lineEditDirectory->setText(dir);
}

//запуск индексации
void MainWindow::on_pushButtonScan_clicked() {
    const QString dir = ui.lineEditDirectory->text();
    if (dir.isEmpty()) {
        statusBar()->showMessage(QString::fromUtf8("Укажите директорию для сканирования"));
        return;
    }
    emit startScan(dir); // ← правильно: запускаем асинхронно в воркере
}

//запуск поиска
void MainWindow::on_pushButtonSearch_clicked() { 
    const QString q = ui.lineEditSearch->text().trimmed();//запрос без пробелов по краям
    if (q.isEmpty()) { //проверка на пустой запрос
        statusBar()->showMessage(QString::fromUtf8("Введите слово для поиска"));
        return;
    }

    const bool regex = ui.checkBoxRegex->isChecked(); //чекбокс: искатьть по реглярному выражению
    const bool caseSens = ui.checkBoxCase->isChecked(); //чекбокс: учитывать регистр 

    
    const QString mask = [this]() -> QString { //маска имени файла
        if (auto* e = this->findChild<QLineEdit*>("lineEditFilter"))
            return e->text();
        return {};
        }();

    const QDate from = ui.dateEditFrom->date(); //фильтр "с даты"
    const QDate to = ui.dateEditTo->date(); //фильтр "по дату"

    QVector<SearchResult> rows = regex //выбор регулярное выражение или точное слово
        ? SearchEngine::searchRegex(&m_db, q, caseSens, mask, from, to)
        : SearchEngine::searchWord(&m_db, q, caseSens, mask, from, to);

    fillResults(rows, q, caseSens); //заполнение таблицы + подсветка
    statusBar()->showMessage(QString::fromUtf8("Найдено: %1").arg(rows.size()));
}

//заполнение таблицы результатов
void MainWindow::fillResults(const QVector<SearchResult>& rows,
    const QString& query, bool caseSensitive)
{
    auto* t = ui.tableWidgetResults;
    t->setRowCount(rows.size()); 

    for (int i = 0; i < rows.size(); ++i) { //для каждой найденой записи 
        const auto& r = rows[i]; 
        t->setItem(i, 0, new QTableWidgetItem(r.file)); // вывод файла
        t->setItem(i, 1, new QTableWidgetItem(QString::number(r.line))); // вывод строки 
        setHighlightedText(t, i, 2, r.fragment, query,
            caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive); // текст строки с подсветкой 
        t->setItem(i, 3, new QTableWidgetItem(r.modified)); // дата изменения
        t->setItem(i, 4, new QTableWidgetItem(QLocale::system().formattedDataSize(r.size))); //размер 
    }
    t->resizeColumnsToContents(); // подгонка ширины колонок
}

// контекстное меню ПКМ 
void MainWindow::on_tableWidgetResults_customContextMenuRequested(const QPoint& pos) {
    auto* t = ui.tableWidgetResults;
    const auto idx = t->indexAt(pos); // узнаем по какой строке кликнули
    if (!idx.isValid()) return;

    const int row = idx.row(); // номер строки результата
    const QString path = t->item(row, 0)->text(); // путь к файлу (столбец 0)

    QString lineText; // текст найденной строки

    if (auto* w = t->cellWidget(row, 2)) { // если в ячейке стоит QLabel с HTML
        if (auto* lbl = qobject_cast<QLabel*>(w)) {
            QTextDocument doc; // создаtм временный документ
            doc.setHtml(lbl->text()); // вставляем HTML из QLabel
            lineText = doc.toPlainText(); // получаем чистый текст без тегов
        }
    }
    else if (auto* it = t->item(row, 2)) {
        lineText = it->text(); // если обычный текст, берём как есть
    }

    QMenu menu(this);
    QAction* actOpen = menu.addAction(QString::fromUtf8("Открыть файл"));
    QAction* actShow = menu.addAction(QString::fromUtf8("Показать в проводнике"));
    QAction* actCopy = menu.addAction(QString::fromUtf8("Скопировать строку"));

    QAction* sel = menu.exec(t->viewport()->mapToGlobal(pos)); // показ меню 
    if (sel == actOpen) { // открыть файл
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
    else if (sel == actShow) { // файл в проводнике
        QProcess::startDetached("explorer.exe", { "/select,", QDir::toNativeSeparators(path) });
    }
    else if (sel == actCopy) { // скопировать в буфер обмена
        QApplication::clipboard()->setText(lineText);
    }
}

void MainWindow::on_actionClearIndex_triggered() { // очитска
    if (m_db.clearAll())
        statusBar()->showMessage(QString::fromUtf8("Индекс очищен"));
}

void MainWindow::on_actionExit_triggered() { close(); } // выход
