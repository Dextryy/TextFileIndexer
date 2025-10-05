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

// ���� ���������� 
void FileIndexer::scanDirectory(const QString& dirPath,
    const QStringList& masks,
    const QByteArray& codec)
{
    QDirIterator it(dirPath, masks, QDir::Files, QDirIterator::Subdirectories); // ���� �� ���������� ����������
    while (it.hasNext())
        processFile(it.next(), codec); // ������������ ������ ����
}

// ��������� ������ �����
void FileIndexer::processFile(const QString& path, const QByteArray& codec) {
    QFile f(path); // ��������� ����
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return; // ���� �� �������� � ����������

    QTextStream in(&f); // ������� ����� ������
    in.setCodec(codec.constData()); // ������ ���������

    QHash<QString, QVector<int>> word2lines; // ����� - ������ �����
    int lineNo = 0; // ������� �����

    while (!in.atEnd()) { // ������ ���������
        const QString line = in.readLine(); ++lineNo; // ��������� ������ � ����������� �����
        QString norm = line.toLower();
        norm.replace(QRegularExpression("[^\\p{L}\\d_\\s]"), " "); // ������� ���������� � �.�.

        auto it = QRegularExpression( // ���������: ����� �� ����/����/_
            "([\\p{L}\\d_]+)", QRegularExpression::UseUnicodePropertiesOption
        ).globalMatch(norm);

        while (it.hasNext()) { // ���� ��� �����
            const QString w = it.next().captured(1);
            if (w.size() >= 2) // ���������� ������� ��������
                word2lines[w].append(lineNo);
        }
    }

    QFileInfo fi(f); // �������� ������ �����
    const int fileId = m_db->upsertFile( // ���������/��������� ������ � �����
        path, fi.size(), fi.lastModified(), lineNo
    );
    if (fileId < 0) return; // ���� �� ���������� � �������

    for (auto it = word2lines.cbegin(); it != word2lines.cend(); ++it) { // ��� ������� �����
        QVector<int> lines = it.value(); // ���� ������ �����
        std::sort(lines.begin(), lines.end()); // ���������
        lines.erase(std::unique(lines.begin(), lines.end()), lines.end()); // ������� �����

        const int wordId = m_db->upsertWord(it.key(), lines.size()); // ��������� ������� �����
        m_db->upsertWordIndex(wordId, fileId, lines); // c�������� ����� ��������� �� ��������
    }
}
