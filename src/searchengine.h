#pragma once
#include <QVector>
#include <QString>
#include <QDate>
#include "dbmanager.h"

struct SearchResult {
    QString file;
    int     line;
    QString fragment;
    QString modified;
    qint64  size = 0;
};

class SearchEngine {
public:
    static QVector<SearchResult> searchWord(DBManager* db,
        const QString& query, bool caseSensitive,
        const QString& fileMask = QString(),
        const QDate& from = QDate(), const QDate& to = QDate());

    static QVector<SearchResult> searchRegex(DBManager* db,
        const QString& pattern, bool caseSensitive,
        const QString& fileMask = QString(),
        const QDate& from = QDate(), const QDate& to = QDate());

private:
    static QString readLine(const QString& path, int lineNo);
    static QString wildcardToLike(QString mask);

    // небольшие общие хелперы для компактности:
    static void appendFilters(QString& sql, const QString& alias,
        const QString& mask, const QDate& from, const QDate& to);
    static void bindFilters(QSqlQuery& q, const QString& mask,
        const QDate& from, const QDate& to);
};
