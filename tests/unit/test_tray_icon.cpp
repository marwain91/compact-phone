#include <gtest/gtest.h>

#include "core/TrayController.h"

#if COMPACTPHONE_WITH_TRAY

#include <QImage>

TEST(TrayIcon, PhoneGlyphRendersFilledSilhouette)
{
    const QImage img = compactphone::TrayController::phoneGlyphImage(22)
                           .convertToFormat(QImage::Format_ARGB32);
    ASSERT_FALSE(img.isNull());
    ASSERT_EQ(img.width(), 22);
    ASSERT_EQ(img.height(), 22);

    int opaque = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(img.pixel(x, y)) > 128) {
                ++opaque;
            }
        }
    }

    // A filled handset silhouette covers roughly a third of the canvas.
    // Near-zero coverage means the embedded SVG failed to parse (the
    // renderer silently draws nothing); near-full means a solid blob.
    const double coverage =
        static_cast<double>(opaque) / (img.width() * img.height());
    EXPECT_GT(coverage, 0.15);
    EXPECT_LT(coverage, 0.6);
}

#endif // COMPACTPHONE_WITH_TRAY
