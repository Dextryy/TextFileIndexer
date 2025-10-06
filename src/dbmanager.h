#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QDateTime>
#include <QString>
#include <QVector>

class DBManager : public QObject {
    Q_OBJECT
public:
    explicit DBManager(QObject* parent = nullptr);

    bool open(const QString& dbPath = "index.db");
    bool clearAll();

    // создаёт таблицы при первом запуске
    bool ensureSchema();

    int  upsertFile(const QString& path, qint64 size,
        const QDateTime& modified, int lineCount);
    int  upsertWord(const QString& wordLower, int addOccurrences);
    bool upsertWordIndex(int wordId, int fileId, const QVector<int>& lines);

    QSqlDatabase database() const { return m_db; }

private:
    QSqlDatabase m_db;

    // общий селект id по строковому полю
    int  selectId(const char* table, const char* col, const QString& value) const;
};
