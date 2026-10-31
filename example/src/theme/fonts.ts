import { Platform } from 'react-native'

// 'monospace' only exists on Android, and iOS silently ignores it.
export const monoFont = Platform.select({ ios: 'Menlo', default: 'monospace' })
