import { Pressable, StyleSheet, Text, View } from 'react-native';

import { useSos } from '../state/SosContext';
import { colors } from '../theme';

export function DongleStatus() {
  const { connected, battery, isMock, toggleMockConnection, toggleMockNodes } = useSos();
  const label = connected ? 'Dongle connected' : 'Dongle not connected';

  return (
    <View style={styles.row}>
      <Pressable
        onLongPress={isMock ? toggleMockConnection : undefined}
        accessibilityRole="text"
        accessibilityLabel={label}
        style={styles.left}
      >
        <View style={[styles.dot, connected ? styles.dotOn : styles.dotOff]} />
        <Text style={styles.label}>{label}</Text>
      </Pressable>
      {connected && battery !== null ? (
        // Mock only: long-press to drop or restore the mesh nodes (WAITING).
        <Pressable onLongPress={isMock ? toggleMockNodes : undefined} accessibilityRole="text">
          <Text style={styles.battery}>Battery {battery}%</Text>
        </Pressable>
      ) : null}
    </View>
  );
}

const styles = StyleSheet.create({
  row: { height: 32, flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center' },
  left: { flexDirection: 'row', alignItems: 'center', gap: 8, minHeight: 32 },
  dot: { width: 10, height: 10, borderRadius: 5 },
  dotOn: { backgroundColor: colors.text },
  dotOff: { borderWidth: 2, borderColor: colors.text },
  label: { color: colors.text, fontSize: 15, fontWeight: '600' },
  battery: { color: colors.muted, fontSize: 15 },
});
