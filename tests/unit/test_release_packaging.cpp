#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>
#include <QTextStream>

namespace {

QString readProjectFile(const QString &relativePath)
{
    QFile file(QStringLiteral(COMPACTPHONE_SOURCE_DIR) + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ADD_FAILURE() << "Could not open " << relativePath.toStdString();
        return {};
    }

    QTextStream in(&file);
    return in.readAll();
}

} // namespace

TEST(ReleasePackaging, MacOSBundleCopiesLicenseResourcesFromExecutableTargetDirectory)
{
    const auto rootCmake = readProjectFile(QStringLiteral("/CMakeLists.txt"));
    const auto appCmake = readProjectFile(QStringLiteral("/src/CMakeLists.txt"));
    ASSERT_FALSE(rootCmake.isEmpty());
    ASSERT_FALSE(appCmake.isEmpty());

    const auto licenseListOffset =
        rootCmake.indexOf(QStringLiteral("set(COMPACTPHONE_LICENCE_FILES"));
    const auto appDirectoryOffset =
        rootCmake.indexOf(QStringLiteral("add_subdirectory(src)"));
    ASSERT_GE(licenseListOffset, 0);
    ASSERT_GE(appDirectoryOffset, 0);
    EXPECT_LT(licenseListOffset, appDirectoryOffset);

    EXPECT_TRUE(appCmake.contains(QStringLiteral(
        "target_sources(compactphone PRIVATE ${COMPACTPHONE_LICENCE_FILES})")));
    EXPECT_TRUE(appCmake.contains(QStringLiteral(
        "set_source_files_properties(${COMPACTPHONE_LICENCE_FILES} PROPERTIES")));
    EXPECT_TRUE(appCmake.contains(QStringLiteral("MACOSX_PACKAGE_LOCATION \"Resources\"")));
}

TEST(ReleasePackaging, SentryEnabledBuildAvoidsUnavailableDefaultPiiSetter)
{
    const auto crashReporting =
        readProjectFile(QStringLiteral("/src/core/CrashReporting.cpp"));
    ASSERT_FALSE(crashReporting.isEmpty());

    EXPECT_FALSE(crashReporting.contains(
        QStringLiteral("sentry_options_set_send_default_pii")));
}

TEST(ReleasePackaging, LinuxReleaseMakesDockerDistArtifactsHostWritable)
{
    const auto workflow =
        readProjectFile(QStringLiteral("/.github/workflows/release-linux.yml"));
    ASSERT_FALSE(workflow.isEmpty());

    const auto bundleOffset =
        workflow.indexOf(QStringLiteral("- name: Bundle into AppImage"));
    const auto chownOffset =
        workflow.indexOf(QStringLiteral("sudo chown -R \"$USER:$USER\" dist"));
    const auto appcastOffset =
        workflow.indexOf(QStringLiteral("- name: Generate Linux appcast"));

    ASSERT_GE(bundleOffset, 0);
    ASSERT_GE(appcastOffset, 0);
    ASSERT_GE(chownOffset, 0);
    EXPECT_LT(bundleOffset, chownOffset);
    EXPECT_LT(chownOffset, appcastOffset);
}

TEST(ReleasePackaging, TrayBuildRequiresQtWidgets)
{
    const auto rootCmake = readProjectFile(QStringLiteral("/CMakeLists.txt"));
    const auto manifest = readProjectFile(QStringLiteral("/vcpkg.json"));
    ASSERT_FALSE(rootCmake.isEmpty());
    ASSERT_FALSE(manifest.isEmpty());

    EXPECT_TRUE(rootCmake.contains(QStringLiteral(
        "find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Svg)")));

    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(manifest.toUtf8(), &error);
    ASSERT_EQ(error.error, QJsonParseError::NoError)
        << error.errorString().toStdString();
    ASSERT_TRUE(doc.isObject());

    QJsonObject qtbase;
    const auto deps = doc.object().value(QStringLiteral("dependencies")).toArray();
    for (const auto &dep : deps) {
        if (dep.isObject()
            && dep.toObject().value(QStringLiteral("name")).toString()
                   == QStringLiteral("qtbase")) {
            qtbase = dep.toObject();
            break;
        }
    }
    ASSERT_FALSE(qtbase.isEmpty()) << "qtbase dependency not found";

    QStringList features;
    const auto featureValues = qtbase.value(QStringLiteral("features")).toArray();
    for (const auto &feature : featureValues) {
        features.push_back(feature.toString());
    }
    EXPECT_TRUE(features.contains(QStringLiteral("widgets")));
}

TEST(ReleasePackaging, WarmCachesDispatchBuildsDepsButNeverSignsOrPublishes)
{
    // A workflow_dispatch with warm_caches_only=true, run on the main ref,
    // builds the expensive vcpkg/Qt deps and saves the caches scoped to main
    // — where every future tag run can restore them (tag-scoped caches are
    // invisible across tags). The run must stop short of signing/publishing.
    const auto macos =
        readProjectFile(QStringLiteral("/.github/workflows/release-macos.yml"));
    const auto windows =
        readProjectFile(QStringLiteral("/.github/workflows/release-windows.yml"));
    ASSERT_FALSE(macos.isEmpty());
    ASSERT_FALSE(windows.isEmpty());

    for (const auto &workflow : {macos, windows}) {
        EXPECT_TRUE(workflow.contains(QStringLiteral("warm_caches_only:")));
        // Tag validation must be skipped: warm runs dispatch off main with a
        // placeholder tag that intentionally fails verify-release-version.py.
        const auto validateOffset =
            workflow.indexOf(QStringLiteral("- name: Validate release tag"));
        ASSERT_GE(validateOffset, 0);
        const auto validateGuard = workflow.indexOf(
            QStringLiteral("if: inputs.warm_caches_only != true"), validateOffset);
        ASSERT_GE(validateGuard, 0);
        const auto validateRun =
            workflow.indexOf(QStringLiteral("run:"), validateOffset);
        EXPECT_LT(validateGuard, validateRun);
    }

    // macOS: the upload step must carry the warm guard.
    const auto macUploadOffset =
        macos.indexOf(QStringLiteral("- name: Upload DMG + appcast to release"));
    ASSERT_GE(macUploadOffset, 0);
    const auto macUploadGuard = macos.indexOf(
        QStringLiteral("if: inputs.warm_caches_only != true"), macUploadOffset);
    const auto macUploadUses =
        macos.indexOf(QStringLiteral("uses:"), macUploadOffset);
    ASSERT_GE(macUploadGuard, 0);
    EXPECT_LT(macUploadGuard, macUploadUses);

    // Windows: warm mode forces publish=true (so deps build) but
    // should_sign=false, and the MSI/upload steps carry the warm guard.
    EXPECT_TRUE(windows.contains(QStringLiteral("WARM_ONLY")));
    EXPECT_TRUE(windows.contains(QStringLiteral(
        "if: steps.signing-config.outputs.publish == 'true' && inputs.warm_caches_only != true")));
    const auto winUploadOffset =
        windows.indexOf(QStringLiteral("- name: Upload MSI + appcast to release"));
    ASSERT_GE(winUploadOffset, 0);
    const auto winUploadGuard = windows.indexOf(
        QStringLiteral("inputs.warm_caches_only != true"), winUploadOffset);
    const auto winUploadUses =
        windows.indexOf(QStringLiteral("uses:"), winUploadOffset);
    ASSERT_GE(winUploadGuard, 0);
    EXPECT_LT(winUploadGuard, winUploadUses);
}

TEST(ReleasePackaging, WindowsReleaseSkipsProductionArtifactWithoutSigningSecret)
{
    const auto workflow =
        readProjectFile(QStringLiteral("/.github/workflows/release-windows.yml"));
    ASSERT_FALSE(workflow.isEmpty());

    const auto configOffset =
        workflow.indexOf(QStringLiteral("- name: Check Windows signing config"));
    const auto restoreOffset =
        workflow.indexOf(QStringLiteral("- name: Restore vcpkg + installed deps cache"));
    const auto uploadOffset =
        workflow.indexOf(QStringLiteral("- name: Upload MSI + appcast to release"));

    ASSERT_GE(configOffset, 0);
    ASSERT_GE(restoreOffset, 0);
    ASSERT_GE(uploadOffset, 0);
    EXPECT_LT(configOffset, restoreOffset);

    EXPECT_TRUE(workflow.contains(QStringLiteral("publish=true")));
    EXPECT_TRUE(workflow.contains(QStringLiteral("publish=false")));
    EXPECT_TRUE(workflow.contains(
        QStringLiteral("if: steps.signing-config.outputs.publish == 'true'")));
    // Signing migrated to Azure Artifact Signing: the production gate now keys
    // off TRUSTED_SIGNING_PROFILE, not the retired CODE_SIGN_THUMBPRINT.
    EXPECT_TRUE(workflow.contains(QStringLiteral(
        "Skipping Windows production artifact because TRUSTED_SIGNING_PROFILE is not configured")));
    EXPECT_FALSE(workflow.contains(QStringLiteral("CODE_SIGN_THUMBPRINT")));
}
