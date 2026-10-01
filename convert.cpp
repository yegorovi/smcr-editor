#include "convert.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>

namespace {

const float kQuality = 0.4f; // VBR ~128 kbps, для музыки хватает

bool writePage(QFile& out, const ogg_page& og, QString* err) {
	if (out.write(reinterpret_cast<const char*>(og.header), og.header_len) != og.header_len
	    || out.write(reinterpret_cast<const char*>(og.body), og.body_len) != og.body_len) {
		if (err) *err = QStringLiteral("Не удалось записать %1: %2").arg(out.fileName(), out.errorString());
		return false;
	}
	return true;
}

// выкинуть готовые страницы из ogg-потока (и в конце дописать хвост)
bool drainStream(ogg_stream_state& os, QFile& out, bool finalFlush, QString* err) {
	ogg_page og;
	while (ogg_stream_pageout(&os, &og) != 0) {
		if (!writePage(out, og, err)) return false;
	}
	if (finalFlush) {
		while (ogg_stream_flush(&os, &og) != 0) {
			if (!writePage(out, og, err)) return false;
		}
	}
	return true;
}

} // namespace

QString mp3CacheDir() {
	const QString dir = QDir::tempPath() + QStringLiteral("/smcr_editor");
	QDir().mkpath(dir);
	return dir;
}

QString mp3CachePathFor(const QString& mp3Path) {
	const QFileInfo fi(mp3Path);
	QString base = fi.completeBaseName();
	for (const QChar& c : base) {
		if (!(c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-')))
			base.replace(c, QLatin1Char('_'));
	}
	if (base.isEmpty()) base = QStringLiteral("track");
	return mp3CacheDir() + QLatin1Char('/') + base + QStringLiteral(".ogg");
}

bool mp3ToOgg(const QString& inPath, const QString& outPath, QString* err) {
	QFile in(inPath);
	if (!in.open(QIODevice::ReadOnly)) {
		if (err) *err = QStringLiteral("Не открыть %1: %2").arg(inPath, in.errorString());
		return false;
	}
	const QByteArray data = in.readAll();
	in.close();
	if (data.isEmpty()) {
		if (err) *err = QStringLiteral("%1 пустой").arg(inPath);
		return false;
	}

	QFile out(outPath);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		if (err) *err = QStringLiteral("Не создать %1: %2").arg(outPath, out.errorString());
		return false;
	}

	mp3dec_t dec;
	mp3dec_init(&dec);

	vorbis_info vi;
	vorbis_comment vc;
	vorbis_dsp_state vd;
	vorbis_block vb;
	ogg_stream_state os;
	bool started = false;
	int rate = 0;
	int ch = 0;

	auto fail = [&](const QString& msg) {
		if (err) *err = msg;
		out.close();
		out.remove();
		if (started) {
			ogg_stream_clear(&os);
			vorbis_block_clear(&vb);
			vorbis_dsp_clear(&vd);
			vorbis_comment_clear(&vc);
			vorbis_info_clear(&vi);
		}
		return false;
	};

	int pos = 0;
	while (pos < data.size()) {
		mp3dec_frame_info_t fi;
		int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
		const int samples = mp3dec_decode_frame(
			&dec, reinterpret_cast<const uint8_t*>(data.constData()) + pos, data.size() - pos, pcm, &fi);
		if (fi.frame_bytes <= 0) break;
		pos += fi.frame_bytes;
		if (samples <= 0) continue;

		if (!started) {
			rate = fi.hz;
			ch = fi.channels;
			if (rate < 8000 || ch < 1 || ch > 2)
				return fail(QStringLiteral("%1: странная лента mp3 (%2 Гц, %3 каналов)")
					            .arg(inPath)
					            .arg(rate)
					            .arg(ch));

			vorbis_info_init(&vi);
			if (vorbis_encode_init_vbr(&vi, ch, rate, kQuality) != 0)
				return fail(QStringLiteral("vorbis_encode_init_vbr не взял %2 Гц").arg(rate));
			vorbis_comment_init(&vc);
			vorbis_comment_add_tag(&vc, "ENCODER", "smcr_editor");
			if (vorbis_analysis_init(&vd, &vi) != 0) return fail(QStringLiteral("vorbis_analysis_init упал"));
			vorbis_block_init(&vd, &vb);
			if (ogg_stream_init(&os, static_cast<int>(QRandomGenerator::global()->generate() & 0x7fffffff)) != 0)
				return fail(QStringLiteral("ogg_stream_init упал"));

			ogg_packet h1, h2, h3;
			vorbis_analysis_headerout(&vd, &vc, &h1, &h2, &h3);
			ogg_stream_packetin(&os, &h1);
			ogg_stream_packetin(&os, &h2);
			ogg_stream_packetin(&os, &h3);
			if (!drainStream(os, out, false, err)) return fail(err ? *err : QStringLiteral("ошибка записи"));
			started = true;
		} else if (fi.hz != rate || fi.channels != ch) {
			return fail(QStringLiteral("%1: mp3 меняет частоту/каналы по ходу (%2/%3 -> %4/%5)")
				            .arg(inPath)
				            .arg(rate)
				            .arg(ch)
				            .arg(fi.hz)
				            .arg(fi.channels));
		}

		float** buffer = vorbis_analysis_buffer(&vd, samples);
		for (int i = 0; i < samples; ++i)
			for (int c = 0; c < ch; ++c)
				buffer[c][i] = static_cast<float>(pcm[i * ch + c]) / 32768.0f;
		vorbis_analysis_wrote(&vd, samples);

		while (vorbis_analysis_blockout(&vd, &vb) == 1) {
			vorbis_analysis(&vb, nullptr);
			vorbis_bitrate_addblock(&vb);
			ogg_packet op;
			while (vorbis_bitrate_flushpacket(&vd, &op) == 1) {
				ogg_stream_packetin(&os, &op);
				if (!drainStream(os, out, false, err)) return fail(err ? *err : QStringLiteral("ошибка записи"));
			}
		}
	}

	if (!started) return fail(QStringLiteral("%1: не нашёл ни одного mp3-кадра").arg(inPath));

	vorbis_analysis_wrote(&vd, 0);
	while (vorbis_analysis_blockout(&vd, &vb) == 1) {
		vorbis_analysis(&vb, nullptr);
		vorbis_bitrate_addblock(&vb);
		ogg_packet op;
		while (vorbis_bitrate_flushpacket(&vd, &op) == 1) {
			ogg_stream_packetin(&os, &op);
		}
	}
	if (!drainStream(os, out, true, err)) return fail(err ? *err : QStringLiteral("ошибка записи"));

	ogg_stream_clear(&os);
	vorbis_block_clear(&vb);
	vorbis_dsp_clear(&vd);
	vorbis_comment_clear(&vc);
	vorbis_info_clear(&vi);
	out.close();
	return true;
}
