#include "searchengine.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDateTime>
#include <QDebug>

// �����
QString SearchEngine::wildcardToLike(QString mask) {
    if (mask.isEmpty()) return mask;
    mask.replace('%', "\\%").replace('_', "\\_");
    mask.replace('*', '%').replace('?', '_');
    return mask;
}

// ������ ���������� ������ �����
QString SearchEngine::readLine(const QString& path, int lineNo) {
    QFile f(path); 
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream in(&f);
    in.setCodec("UTF-8"); // ���������/UTF-8
    for (int i = 1; i < lineNo && !in.atEnd(); ++i) in.readLine(); // ������������ N-1 �����
    return in.atEnd() ? QString() : in.readLine(); // ���������� ������ ������ (��� �����)
}

// ���������� �������� � SQL
void SearchEngine::appendFilters(QString& sql, const QString& alias,
    const QString& mask, const QDate& from, const QDate& to)
{
    if (!mask.isEmpty()) sql += QString("AND %1.path LIKE :mask ESCAPE '\\' ").arg(alias); // ������ �� �����
    if (from.isValid())  sql += QString("AND %1.modified >= :from ").arg(alias); // ������ "� ����"
    if (to.isValid())    sql += QString("AND %1.modified <= :to ").arg(alias); // ������ "�� ����"
}

// �������� �������� ��������
void SearchEngine::bindFilters(QSqlQuery& q, const QString& mask,
    const QDate& from, const QDate& to)
{
    if (!mask.isEmpty()) q.bindValue(":mask", wildcardToLike(mask));
    if (from.isValid())  q.bindValue(":from", QDateTime(from, QTime(0, 0)).toString(Qt::ISODate));
    if (to.isValid())    q.bindValue(":to", QDateTime(to, QTime(23, 59, 59)).toString(Qt::ISODate));
}

// ����� �� �����
QVector<SearchResult> SearchEngine::searchWord(DBManager* db,
    const QString& query, bool caseSensitive,
    const QString& fileMask, const QDate& from, const QDate& to)
{
    QVector<SearchResult> out; // �������� ����������
    if (!db || query.isEmpty()) return out; 
    const QString word = caseSensitive ? query : query.toLower(); // ������� ����� � ������ ��������
    QString sql = // ������������� �� ��������� ��������
        "SELECT f.path, f.modified, f.size, wi.line_numbers "
        "FROM WordIndex wi "
        "JOIN Words w ON w.id = wi.word_id "
        "JOIN Files f ON f.id = wi.file_id "
        "WHERE w.word = :word ";
    appendFilters(sql, "f", fileMask, from, to);

    QSqlQuery q(db->database()); // ������� ������
    q.prepare(sql);
    q.bindValue(":word", word); // �������� ��������� :word
    bindFilters(q, fileMask, from, to); // �������� :mask/:from/:to
    if (!q.exec()) { qWarning() << q.lastError(); return out; } // ���� ������ � ����� ������ ������

    while (q.next()) { // ���� �� �����������
        const QString path = q.value(0).toString(); // ����
        const QString modified = q.value(1).toString(); // ���� ��������� 
        const qint64  size = q.value(2).toLongLong(); // ������ � ������
        const QString linesStr = q.value(3).toString(); // ������

        const QStringList parts = linesStr.split(',', Qt::SkipEmptyParts); // ��������� �� ������
        for (const QString& p : parts) { // ��� ������� ������ ������
            const int lineNo = p.toInt(); // ��������� � int
            const QString lineText = readLine(path, lineNo); // ������ ���������� ������
            if (!lineText.isEmpty())
                out.push_back({ path, lineNo, lineText, modified, size }); // ��������� ���������
        }
    }
    return out;
}

// ����� �� ����������� ���������
QVector<SearchResult> SearchEngine::searchRegex(DBManager* db,
    const QString& pattern, bool caseSensitive,
    const QString& fileMask, const QDate& from, const QDate& to)
{
    QVector<SearchResult> out;
    if (!db || pattern.isEmpty()) return out; // ��� ������� � ��� ������

    QString sql = "SELECT path, modified, size FROM Files WHERE 1=1 "; // ���� ������ ������
    appendFilters(sql, "Files", fileMask, from, to); // ������������ �� �����/�����

    QSqlQuery q(db->database()); // ������ 
    q.prepare(sql);
    bindFilters(q, fileMask, from, to); // ����������� ���������
    if (!q.exec()) { qWarning() << q.lastError(); return out; } // ������ � �����

    QRegularExpression::PatternOptions opts = QRegularExpression::UseUnicodePropertiesOption;
    if (!caseSensitive) opts |= QRegularExpression::CaseInsensitiveOption; 
    
    QRegularExpression re(pattern, opts); // ����������� �������
    while (q.next()) {
        const QString path = q.value(0).toString();
        const QString modified = q.value(1).toString();
        const qint64  size = q.value(2).toLongLong();

        QFile f(path); // ��������� ����
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QTextStream in(&f); in.setCodec("UTF-8"); // ���������/UTF-8

        int lineNo = 0;
        while (!in.atEnd()) { // ������ ���������
            const QString line = in.readLine(); ++lineNo;
            if (re.match(line).hasMatch()) // ���� ������� ����� ����������
                out.push_back({ path, lineNo, line, modified, size }); // ��������� ���������
        }
    }
    return out;
}
