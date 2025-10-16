#include "dbmanager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QThread>

namespace {
    inline bool execWarn(QSqlQuery& q) {
        if (!q.exec()) { qWarning() << q.lastError(); return false; }
        return true;
    }

    QString joinLines(const QVector<int>& lines) {
        QStringList parts; parts.reserve(lines.size());
        for (int n : lines) parts << QString::number(n);
        return parts.join(',');
    }
}

DBManager::DBManager(QObject* parent) : QObject(parent) {}

//открытие и подготовка файла БД 
bool DBManager::open(const QString& dbPath) {
    QString finalPath = dbPath;
    QFileInfo fi(dbPath);
    if (fi.isRelative()) { 
        // создаём путь рядом с исполняемым
        finalPath = QDir(QCoreApplication::applicationDirPath()).filePath(dbPath);
    }
    const QString connName =
        QString::fromLatin1("app_%1").arg(quintptr(QThread::currentThreadId()));
    m_db = QSqlDatabase::contains(connName) // переиспользуем подключение "app", если уже есть
        ? QSqlDatabase::database(connName)
        : QSqlDatabase::addDatabase("QSQLITE", connName); // иначе создаём новое подключение SQLite

    m_db.setDatabaseName(finalPath); // файл БД (создастся при первом открытии)
    if (!m_db.open()) { qWarning() << "SQLite open error:" << m_db.lastError(); return false; }

    QSqlQuery pragma(m_db); // запрос для PRAGMA
    pragma.exec("PRAGMA foreign_keys = ON;"); // включаем внешние ключи 
    return ensureSchema();
}

//создание таблиц
bool DBManager::ensureSchema() {
    QSqlQuery q(m_db);

    q.prepare( // таблица Files: сведения о каждом файле
        "CREATE TABLE IF NOT EXISTS Files ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " path TEXT NOT NULL UNIQUE,"
        " size INTEGER,"
        " modified TEXT,"
        " line_count INTEGER)"
    ); if (!execWarn(q)) return false;

    q.prepare( // таблица Words: уникальное слово и его общий счётчик
        "CREATE TABLE IF NOT EXISTS Words (" 
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " word TEXT NOT NULL UNIQUE,"
        " occurrences INTEGER NOT NULL DEFAULT 0)"
    ); if (!execWarn(q)) return false;

    q.prepare( // таблица WordIndex: связи слово—файл и список строк
        "CREATE TABLE IF NOT EXISTS WordIndex ("
        " word_id INTEGER NOT NULL,"
        " file_id INTEGER NOT NULL,"
        " line_numbers TEXT NOT NULL,"
        " PRIMARY KEY(word_id, file_id),"
        " FOREIGN KEY(word_id) REFERENCES Words(id) ON DELETE CASCADE,"
        " FOREIGN KEY(file_id) REFERENCES Files(id) ON DELETE CASCADE)"
    ); if (!execWarn(q)) return false;

    return true;
}

//очистка трех таблиц
bool DBManager::clearAll() {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM WordIndex"); if (!execWarn(q)) return false;
    q.prepare("DELETE FROM Words");     if (!execWarn(q)) return false;
    q.prepare("DELETE FROM Files");     if (!execWarn(q)) return false;
    return true;
}


int DBManager::upsertFile(const QString& path, qint64 size,
    const QDateTime& modified, int lineCount) {
    int id = selectId("Files", "path", path); // пытаемся найти существующую запись
    QSqlQuery q(m_db);

    if (id < 0) { // // если нет — INSERT
        q.prepare("INSERT INTO Files(path,size,modified,line_count)"
            " VALUES(:p,:s,:m,:lc)");
        q.bindValue(":p", path); // связываем параметры (path, size, modified, lineCount)
        q.bindValue(":s", size);
        q.bindValue(":m", modified.toString(Qt::ISODate));
        q.bindValue(":lc", lineCount);
        if (!execWarn(q)) return -1; // при ошибке - лог и -1
        return q.lastInsertId().toInt(); // возвращаем новый id
    }

    q.prepare("UPDATE Files SET size=:s, modified=:m, line_count=:lc WHERE id=:id"); // иначе — UPDATE
    q.bindValue(":s", size);
    q.bindValue(":m", modified.toString(Qt::ISODate));
    q.bindValue(":lc", lineCount);
    q.bindValue(":id", id);
    execWarn(q); // обновляем
    return id; // возвращаем существующий id
}

int DBManager::upsertWord(const QString& wordLower, int addOccurrences) {
    int id = selectId("Words", "word", wordLower); // ищем слово в нижнем регистре
    QSqlQuery q(m_db);

    if (id < 0) { // вставка нового слова
        q.prepare("INSERT INTO Words(word,occurrences) VALUES(:w,:occ)");
        q.bindValue(":w", wordLower);
        q.bindValue(":occ", addOccurrences);
        if (!execWarn(q)) return -1;
        return q.lastInsertId().toInt(); // id нового слова
    }

    q.prepare("UPDATE Words SET occurrences = occurrences + :occ WHERE id=:id"); // Увеличиваем счётчик
    q.bindValue(":occ", addOccurrences);
    q.bindValue(":id", id);
    execWarn(q);
    return id; // возвращаем существующий id
}

bool DBManager::upsertWordIndex(int wordId, int fileId, const QVector<int>& lines) {
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO WordIndex(word_id,file_id,line_numbers)"
        " VALUES(:w,:f,:L)");
    q.bindValue(":w", wordId);
    q.bindValue(":f", fileId);
    q.bindValue(":L", joinLines(lines));
    return execWarn(q);
}

int DBManager::selectId(const char* table, const char* col, const QString& value) const {
    QSqlQuery q(m_db);
    q.prepare(QString("SELECT id FROM %1 WHERE %2 = :v").arg(table, col));
    q.bindValue(":v", value);
    if (!q.exec()) return -1;
    return q.next() ? q.value(0).toInt() : -1;
}
