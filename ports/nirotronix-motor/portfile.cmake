vcpkg_download_distfile(archive
    URLS "https://github.com/abdallah-shehawey/NiroTronixRaspberryPi/releases/download/v${VERSION}/NiroTronix-Raspberry-Pi-Motor-Control.zip"
    FILENAME "NiroTronix-Raspberry-Pi-Motor-Control-${VERSION}.zip"
    SHA512 1aa4b43b13128fdec601574b019d81e1c7f4c0d6f52e907787dca3b9f4b911848864769c8b65c1f5ce5e610d086caab59e38d49117e591da6f5f012f0b1a342e
)
vcpkg_extract_source_archive(source_path ARCHIVE "${archive}")

set(VCPKG_BUILD_TYPE release)
vcpkg_cmake_configure(
    SOURCE_PATH "${source_path}/cpp"
    OPTIONS -DNIROTRONIX_BUILD_DEMO=OFF -DCMAKE_INSTALL_LIBDIR=lib
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME NiroTronixMotor CONFIG_PATH lib/cmake/NiroTronixMotor)
set(VCPKG_POLICY_SKIP_COPYRIGHT_CHECK enabled)
