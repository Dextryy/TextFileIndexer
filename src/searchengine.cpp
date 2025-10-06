#include "searchengine.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDateTime>
#include <QDebug>

// маска
QString SearchEngine::wildcardToLike(QString mask) {
    if (mask.isEmpty()) return mask;
    mask.replace('%', "\\%").replace('_', "\\_");
    mask.replace('*', '%').replace('?', '_');
    return mask;
}

// чтение конкретной строки файла
QString SearchEngine::readLine(const QString& path, int lineNo) {
    QFile f(path); 
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream in(&f);
    in.setCodec("UTF-8"); // кириллица/UTF-8
    for (int i = 1; i < lineNo && !in.atEnd(); ++i) in.readLine(); // пролистываем N-1 строк
    return in.atEnd() ? QString() : in.readLine(); // возвращаем нужную строку (или пусто)
}

// добавление фильтров к SQL
void SearchEngine::appendFilters(QString& sql, const QString& alias,
    const QString& mask, const QDate& from, const QDate& to)
{
    if (!mask.isEmpty()) sql += QString("AND %1.path LIKE :mask ESCAPE '\\' ").arg(alias); // фильтр по имени
    if (from.isValid())  sql += QString("AND %1.modified >= :from ").arg(alias); // фильтр "с даты"
    if (to.isValid())    sql += QString("AND %1.modified <= :to ").arg(alias); // фильтр "по дату"
}

// привязка значений фильтров
void SearchEngine::bindFilters(QSqlQuery& q, const QString& mask,
    const QDate& from, const QDate& to)
{
    if (!mask.isEmpty()) q.bindValue(":mask", wildcardToLike(mask));
    if (from.isValid())  q.bindValue(":from", QDateTime(from, QTime(0, 0)).toString(Qt::ISODate));
    if (to.isValid())    q.bindValue(":to", QDateTime(to, QTime(23, 59, 59)).toString(Qt::ISODate));
}

// поиск по слову
QVector<SearchResult> SearchEngine::searchWord(DBManager* db,
    const QString& query, bool caseSensitive,
    const QString& fileMask, const QDate& from, const QDate& to)
{
    QVector<SearchResult> out; // собираем результаты
    if (!db || query.isEmpty()) return out; 
    const QString word = caseSensitive ? query : query.toLower(); // готовим слово с учётом регистра
    QString sql = // присоединение по индексным таблицам
        "SELECT f.path, f.modified, f.size, wi.line_numbers "
        "FROM WordIndex wi "
        "JOIN Words w ON w.id = wi.word_id "
        "JOIN Files f ON f.id = wi.file_id "
        "WHERE w.word = :word ";
    appendFilters(sql, "f", fileMask, from, to);

    QSqlQuery q(db->database()); // готовим запрос
    q.prepare(sql);
    q.bindValue(":word", word); // привязка параметра :word
    bindFilters(q, fileMask, from, to); // привязка :mask/:from/:to
    if (!q.exec()) { qWarning() << q.lastError(); return out; } // если ошибка — отдаём пустой список

    while (q.next()) { // идем по результатам
        const QString path = q.value(0).toString(); // путь
        const QString modified = q.value(1).toString(); // дата изменения 
        const qint64  size = q.value(2).toLongLong(); // размер в байтах
        const QString linesStr = q.value(3).toString(); // строка

        const QStringList parts = linesStr.split(',', Qt::SkipEmptyParts); // разбиваем на номера
        for (const QString& p : parts) { // для каждого номера строки
            const int lineNo = p.toInt(); // переводим в int
            const QString lineText = readLine(path, lineNo); // читаем конкретную строку
            if (!lineText.isEmpty())
                out.push_back({ path, lineNo, lineText, modified, size }); // добавляем результат
        }
    }
    return out;
}

// поиск по регулярному выражению
QVector<SearchResult> SearchEngine::searchRegex(DBManager* db,
    const QString& pattern, bool caseSensitive,
    const QString& fileMask, const QDate& from, const QDate& to)
{
    QVector<SearchResult> out;
    if (!db || pattern.isEmpty()) return out; // без шаблона — нет поиска

    QString sql = "SELECT path, modified, size FROM Files WHERE 1=1 "; // берём список файлов
    appendFilters(sql, "Files", fileMask, from, to); // ограничиваем по маске/датам

    QSqlQuery q(db->database()); // запрос 
    q.prepare(sql);
    bindFilters(q, fileMask, from, to); // привязываем параметры
    if (!q.exec()) { qWarning() << q.lastError(); return out; } // ошибка — пусто

    QRegularExpression::PatternOptions opts = QRegularExpression::UseUnicodePropertiesOption;
    if (!caseSensitive) opts |= QRegularExpression::CaseInsensitiveOption; 
    
    QRegularExpression re(pattern, opts); // компилируем паттерн
    while (q.next()) {
        const QString path = q.value(0).toString();
        const QString modified = q.value(1).toString();
        const qint64  size = q.value(2).toLongLong();

        QFile f(path); // открываем файл
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QTextStream in(&f); in.setCodec("UTF-8"); // кириллица/UTF-8

        int lineNo = 0;
        while (!in.atEnd()) { // читаем построчно
            const QString line = in.readLine(); ++lineNo;
            if (re.match(line).hasMatch()) // если паттерн нашёл совпадение
                out.push_back({ path, lineNo, line, modified, size }); // добавляем результат
        }
    }
    return out;
}
