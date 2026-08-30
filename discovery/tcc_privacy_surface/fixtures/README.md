# Fixture homes for native_runner tests.
#
# Tests create temporary directories at runtime and set HOME before invoking
# tcc_privacy_surface. Populated homes touch:
#   ~/Library/Application Support/com.apple.TCC/TCC.db
#   ~/Library/Preferences/com.apple.security.FDERecoveryKeyEscrow.plist
#   ~/Library/Preferences/com.apple.ScreenTimeAgent.plist
