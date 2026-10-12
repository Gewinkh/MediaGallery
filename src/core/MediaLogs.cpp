#include "core/MediaLogs.h"

#include <QLibrary>
#include <QtGlobal>
#include <mutex>

namespace mg::media {
namespace {

//  AV_LOG_ERROR. Der Wert ist Teil von FFmpegs ABI und seit jeher 16;
//  eingebunden wird der Kopf nicht, sonst waere FFmpeg eine Bauabhaengigkeit.
constexpr int kNurFehler = 16;

//  Die Fassungsnummer steht im Dateinamen, und welche danebenliegt, entscheidet
//  die Distribution. Von neu nach alt, bis eine sich laden laesst; findet sich
//  keine, bleibt alles wie es war.
constexpr int kNeuste = 62;
constexpr int kAelteste = 55;

}  // namespace

void beQuiet() {
    static std::once_flag einmal;
    std::call_once(einmal, [] {
        for (int fassung = kNeuste; fassung >= kAelteste; --fassung) {
            QLibrary lib(QStringLiteral("avutil"), fassung);
            if (!lib.load()) continue;
            if (auto* setzen = reinterpret_cast<void (*)(int)>(
                    lib.resolve("av_log_set_level"))) {
                setzen(kNurFehler);
                return;
            }
        }
    });
}

void limitHwProbe() {
#if defined(Q_OS_LINUX)
    //  Leer = Software: VDPAU (hier nur NVIDIA) hing nach einem Dekoderfehler (Xid 68), `setSource` wartete ewig.
    if (!qEnvironmentVariableIsSet("QT_FFMPEG_DECODING_HW_DEVICE_TYPES"))
        qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "");
    if (!qEnvironmentVariableIsSet("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES"))
        qputenv("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES", "");   // die App kodiert kein Video
#endif
}

}  // namespace mg::media
