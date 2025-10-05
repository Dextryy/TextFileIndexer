#include "fileindexer.h"
#include <QDirIterator>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>


FileIndexer::FileIndexer(DBManager* db, QObject* parent)
    : QObject(parent), m_db(db) {
}

// скан директории 
void FileIndexer::scanDirectory(const QString& dirPath,
    const QStringList& masks,
    const QByteArray& codec)
{
    QDirIterator it(dirPath, masks, QDir::Files, QDirIterator::Subdirectories); // идем по директории рекурсивно
    while (it.hasNext())
        processFile(it.next(), codec); // обрабатываем каждый файл
}

// обработка одного файла
void FileIndexer::processFile(const QString& path, const QByteArray& codec) {
    QFile f(path); // открываем файл
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return; // если не открылс€ Ч пропускаем

    QTextStream in(&f); // готовим поток чтени€
    in.setCodec(codec.constData()); // ставим кодировку

    QHash<QString, QVector<int>> word2lines; // слово - номера строк
    int lineNo = 0; // счЄтчик строк

    while (!in.atEnd()) { // „итаем построчно
        const QString line = in.readLine(); ++lineNo; // считываем строку и увеличиваем номер
        QString norm = line.toLower();
        norm.replace(QRegularExpression("[^\\p{L}\\d_\\s]"), " "); // убираем пунктуацию и т.п.

        auto it = QRegularExpression( // регул€рка: слова из букв/цифр/_
            "([\\p{L}\\d_]+)", QRegularExpression::UseUnicodePropertiesOption
        ).globalMatch(norm);

        while (it.hasNext()) { // ищем все слова
            const QString w = it.next().captured(1);
            if (w.size() >= 2) // игнорируем слишком короткие
                word2lines[w].append(lineNo);
        }
    }

    QFileInfo fi(f); // собираем данные файла
    const int fileId = m_db->upsertFile( // вставл€ем/обновл€ем запись о файле
        path, fi.size(), fi.lastModified(), lineNo
    );
    if (fileId < 0) return; // если не получилось Ч выходим

    for (auto it = word2lines.cbegin(); it != word2lines.cend(); ++it) { // дл€ каждого слова
        QVector<int> lines = it.value(); // берЄм список строк
        std::sort(lines.begin(), lines.end()); // сортируем
        lines.erase(std::unique(lines.begin(), lines.end()), lines.end()); // убираем дубли

        const int wordId = m_db->upsertWord(it.key(), lines.size()); // обновл€ем счЄтчик слова
        m_db->upsertWordIndex(wordId, fileId, lines); // cохран€ем св€зь словоЧфайл со строками
    }
}
