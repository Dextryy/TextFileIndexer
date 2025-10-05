#pragma once
#include <QObject>
#include <QStringList>
#include <QVector>
#include "dbmanager.h"

class FileIndexer : public QObject {
    Q_OBJECT
public:
    explicit FileIndexer(DBManager* db, QObject* parent = nullptr);

    void scanDirectory(const QString& dirPath,
        const QStringList& masks = { "*.txt","*.log","*.csv" },
        const QByteArray& codec = "UTF-8");

private:
    DBManager* m_db;

    void processFile(const QString& path, const QByteArray& codec);
};
