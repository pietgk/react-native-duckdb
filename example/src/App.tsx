import React from 'react'
import { StatusBar } from 'react-native'
import { NavigationContainer, DefaultTheme, DarkTheme } from '@react-navigation/native'
import { KeyboardProvider } from 'react-native-keyboard-controller'
import './tests/build.test'
import './tests/lifecycle.test'
import './tests/errors.test'
import './tests/query.test'
import './tests/transaction.test'
import './tests/types.test'
import './tests/columnar.test'
import './tests/streaming.test'
import './tests/appender.test'
import './tests/filequery.test'
import './tests/extensions.test'
import './tests/cancel.test'
import './tests/namedparams.test'
import './tests/profiling.test'
import './tests/progress.test'
import './tests/remote.test'
import './tests/fts.test'
import './tests/vss.test'
import './tests/paths.test'
import './tests/robustness.test'
import './tests/benchmark.test'
import { ThemeProvider, useTheme } from './theme'
import { TabNavigator } from './navigation/TabNavigator'
import { useTestAutorun } from './testing/useTestAutorun'

function AppContent() {
  const { colors, isDark } = useTheme()
  useTestAutorun()

  const navTheme = {
    ...(isDark ? DarkTheme : DefaultTheme),
    colors: {
      ...(isDark ? DarkTheme : DefaultTheme).colors,
      primary: colors.tabBarActiveTint,
      background: colors.background,
      card: colors.surface,
      text: colors.text,
      border: colors.border,
      notification: colors.error,
    },
  }

  return (
    <NavigationContainer theme={navTheme}>
      <StatusBar barStyle={isDark ? 'light-content' : 'dark-content'} />
      <TabNavigator />
    </NavigationContainer>
  )
}

export default function App() {
  return (
    <ThemeProvider>
      <KeyboardProvider>
        <AppContent />
      </KeyboardProvider>
    </ThemeProvider>
  )
}
