#include "ogg.h"

#include <QFile>
#include <QHash>

#include <cstring>

namespace {

struct Stream {
	double rate = 0;
	double preSkip = 0;
	double lastGranule = -1;
	bool known = false;
};

quint16 rd16(const char* p) {
	return quint16(quint8(p[0])) | (quint16(quint8(p[1])) << 8);
}

quint32 rd32(const char* p) {
	return quint32(quint8(p[0])) | (quint32(quint8(p[1])) << 8)
		| (quint32(quint8(p[2])) << 16) | (quint32(quint8(p[3])) << 24);
}

quint64 rd64(const char* p) {
	return quint64(rd32(p)) | (quint64(rd32(p + 4)) << 32);
}

}

double oggDurationSeconds(const QString& path, QString* err) {
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		if (err) *err = QStringLiteral("Не открыть %1").arg(path);
		return 0;
	}
	const QByteArray data = f.readAll();
	f.close();
	if (data.size() < 27) {
		if (err) *err = QStringLiteral("Слишком мал: %1").arg(path);
		return 0;
	}

	QHash<quint32, Stream> streams;
	const char* base = data.constData();
	const int size = data.size();
	int pos = 0;

	while (pos + 27 <= size) {
		pos = data.indexOf("OggS", pos);
		if (pos < 0 || pos + 27 > size) break;
		if (quint8(base[pos + 4]) != 0) {
			pos += 4;
			continue;
		}
		const quint64 granule = rd64(base + pos + 6);
		const quint32 serial = rd32(base + pos + 14);
		const quint32 seq = rd32(base + pos + 18);
		const int segCount = quint8(base[pos + 26]);
		if (pos + 27 + segCount > size) break;
		int payloadLen = 0;
		for (int i = 0; i < segCount; ++i)
			payloadLen += quint8(base[pos + 27 + i]);
		const int pageEnd = pos + 27 + segCount + payloadLen;
		if (pageEnd > size) break;

		const char* pay = base + pos + 27 + segCount;
		Stream& s = streams[serial];
		if (seq == 0 && payloadLen >= 16 && quint8(pay[0]) == 1 && std::memcmp(pay + 1, "vorbis", 6) == 0) {
			const double rate = double(rd32(pay + 12));
			if (rate > 0) {
				s.rate = rate;
				s.preSkip = 0;
				s.known = true;
			}
		} else if (seq == 0 && payloadLen >= 16 && std::memcmp(pay, "OpusHead", 8) == 0) {
			s.rate = 48000.0;
			s.preSkip = double(rd16(pay + 10));
			s.known = true;
		}
		if (granule != 0xFFFFFFFFFFFFFFFFULL)
			s.lastGranule = qMax(s.lastGranule, double(granule));

		pos = pageEnd;
	}

	double samaya_dolgaya = 0;
	for (auto it = streams.constBegin(); it != streams.constEnd(); ++it) {
		const Stream& s = it.value();
		if (!s.known || s.rate <= 0 || s.lastGranule < 0) continue;
		const double d = (s.lastGranule - s.preSkip) / s.rate;
		if (d > samaya_dolgaya) samaya_dolgaya = d;
	}
	if (samaya_dolgaya <= 0) {
		if (err) *err = QStringLiteral("Не удалось определить длительность: %1").arg(path);
		return 0;
	}
	return samaya_dolgaya;
}

int oggDurationRounded(const QString& path, QString* err) {
	const double d = oggDurationSeconds(path, err);
	if (d <= 0) return 0;
	const int r = int(d + 0.5);
	return r < 1 ? 1 : r;
}
