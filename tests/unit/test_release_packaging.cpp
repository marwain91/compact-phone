#include <gtest/gtest.h>

#include <QFile>
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
