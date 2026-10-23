import type { ReactNode } from 'react';
import { ScrollView, StyleSheet } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import { colors, SCREEN_PADDING } from '../theme';

// Scrolls only when a small phone can't fit the layout; on a normal phone the
// content fills the screen so `marginTop: 'auto'` pins footers to the bottom.
export function Screen({ children }: { children: ReactNode }) {
  return (
    <SafeAreaView style={styles.safe}>
      <ScrollView contentContainerStyle={styles.content} keyboardShouldPersistTaps="handled">
        {children}
      </ScrollView>
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  safe: { flex: 1, backgroundColor: colors.bg },
  content: { flexGrow: 1, paddingHorizontal: SCREEN_PADDING, paddingTop: 20, paddingBottom: 24 },
});
