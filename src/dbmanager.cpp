#include "dbmanager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

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

//�������� � ���������� ����� �� 
bool DBManager::open(const QString& dbPath) {
    m_db = QSqlDatabase::contains("app") // �������������� ����������� "app", ���� ��� ����
        ? QSqlDatabase::database("app")
        : QSqlDatabase::addDatabase("QSQLITE", "app"); // ����� ������ ����� ����������� SQLite

    m_db.setDatabaseName(dbPath); // ���� �� (��������� ��� ������ ��������)
    if (!m_db.open()) { qWarning() << "SQLite open error:" << m_db.lastError(); return false; }

    QSqlQuery pragma(m_db); // ������ ��� PRAGMA
    pragma.exec("PRAGMA foreign_keys = ON;"); // �������� ������� �����
    return ensureSchema();
}

//�������� ������
bool DBManager::ensureSchema() {
    QSqlQuery q(m_db);

    q.prepare( // ������� Files: �������� � ������ �����
        "CREATE TABLE IF NOT EXISTS Files ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " path TEXT NOT NULL UNIQUE,"
        " size INTEGER,"
        " modified TEXT,"
        " line_count INTEGER)"
    ); if (!execWarn(q)) return false;

    q.prepare( // ������� Words: ���������� ����� � ��� ����� �������
        "CREATE TABLE IF NOT EXISTS Words (" 
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " word TEXT NOT NULL UNIQUE,"
        " occurrences INTEGER NOT NULL DEFAULT 0)"
    ); if (!execWarn(q)) return false;

    q.prepare( // ������� WordIndex: ����� ��������� � ������ �����
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

//������� ���� ������
bool DBManager::clearAll() {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM WordIndex"); if (!execWarn(q)) return false;
    q.prepare("DELETE FROM Words");     if (!execWarn(q)) return false;
    q.prepare("DELETE FROM Files");     if (!execWarn(q)) return false;
    return true;
}


int DBManager::upsertFile(const QString& path, qint64 size,
    const QDateTime& modified, int lineCount) {
    int id = selectId("Files", "path", path); // �������� ����� ������������ ������
    QSqlQuery q(m_db);

    if (id < 0) { // // ���� ��� � INSERT
        q.prepare("INSERT INTO Files(path,size,modified,line_count)"
            " VALUES(:p,:s,:m,:lc)");
        q.bindValue(":p", path); // ��������� ��������� (path, size, modified, lineCount)
        q.bindValue(":s", size);
        q.bindValue(":m", modified.toString(Qt::ISODate));
        q.bindValue(":lc", lineCount);
        if (!execWarn(q)) return -1; // ��� ������ - ��� � -1
        return q.lastInsertId().toInt(); // ���������� ����� id
    }

    q.prepare("UPDATE Files SET size=:s, modified=:m, line_count=:lc WHERE id=:id"); // ����� � UPDATE
    q.bindValue(":s", size);
    q.bindValue(":m", modified.toString(Qt::ISODate));
    q.bindValue(":lc", lineCount);
    q.bindValue(":id", id);
    execWarn(q); // ���������
    return id; // ���������� ������������ id
}

int DBManager::upsertWord(const QString& wordLower, int addOccurrences) {
    int id = selectId("Words", "word", wordLower); // ���� ����� � ������ ��������
    QSqlQuery q(m_db);

    if (id < 0) { // ������� ������ �����
        q.prepare("INSERT INTO Words(word,occurrences) VALUES(:w,:occ)");
        q.bindValue(":w", wordLower);
        q.bindValue(":occ", addOccurrences);
        if (!execWarn(q)) return -1;
        return q.lastInsertId().toInt(); // id ������ �����
    }

    q.prepare("UPDATE Words SET occurrences = occurrences + :occ WHERE id=:id"); // ����������� �������
    q.bindValue(":occ", addOccurrences);
    q.bindValue(":id", id);
    execWarn(q);
    return id; // ���������� ������������ id
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
