import { Stack } from 'expo-router';
import { StatusBar } from 'expo-status-bar';

import { SosProvider } from '../state/SosContext';
import { colors } from '../theme';

export default function RootLayout() {
  return (
    <SosProvider>
      <StatusBar style="light" />
      <Stack screenOptions={{ headerShown: false, contentStyle: { backgroundColor: colors.bg }, animation: 'fade' }}>
        <Stack.Screen name="index" />
        <Stack.Screen name="countdown" />
        <Stack.Screen name="status" options={{ gestureEnabled: false }} />
      </Stack>
    </SosProvider>
  );
}
