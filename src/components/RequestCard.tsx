import { StyleSheet, Text, View } from 'react-native';

import { colors } from '../theme';

export function RequestCard({ summary }: { summary: string }) {
  return (
    <View style={styles.card}>
      <Text style={styles.label}>Your request</Text>
      <Text style={styles.summary}>{summary}</Text>
    </View>
  );
}

const styles = StyleSheet.create({
  card: { borderWidth: 1.5, borderColor: colors.cardBorder, borderRadius: 16, paddingVertical: 14, paddingHorizontal: 16 },
  label: { color: colors.muted, fontSize: 14 },
  summary: { color: colors.text, fontSize: 17, lineHeight: 24, marginTop: 4 },
});
