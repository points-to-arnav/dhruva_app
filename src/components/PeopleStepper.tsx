import { useState } from 'react';
import { Pressable, StyleSheet, Text, TextInput, View } from 'react-native';

import { MAX_PEOPLE, MIN_PEOPLE } from '../state/sos';
import { colors } from '../theme';

type Props = { value: number; onChange: (value: number) => void };

export function PeopleStepper({ value, onChange }: Props) {
  // What the user is typing, so the field can be empty mid-edit; null when
  // not editing. Committed (and clamped by the parent) on blur.
  const [draft, setDraft] = useState<string | null>(null);

  const commit = () => {
    if (draft === null) return;
    const parsed = parseInt(draft, 10);
    if (!Number.isNaN(parsed)) onChange(parsed);
    setDraft(null);
  };

  return (
    <View style={styles.row}>
      <View>
        <Text style={styles.title}>People with me</Text>
        <Text style={styles.hint}>Up to {MAX_PEOPLE}</Text>
      </View>
      <View style={styles.controls}>
        <Pressable
          onPress={() => onChange(value - 1)}
          disabled={value <= MIN_PEOPLE}
          accessibilityRole="button"
          accessibilityLabel="Fewer people"
          style={({ pressed }) => [styles.round, (pressed || value <= MIN_PEOPLE) && styles.dim]}
        >
          <Text style={styles.sign}>−</Text>
        </Pressable>
        <TextInput
          value={draft ?? String(value)}
          onChangeText={(t) => setDraft(t.replace(/[^0-9]/g, ''))}
          onBlur={commit}
          onSubmitEditing={commit}
          keyboardType="number-pad"
          maxLength={3}
          accessibilityLabel="Number of people"
          style={styles.input}
          selectTextOnFocus
        />
        <Pressable
          onPress={() => onChange(value + 1)}
          disabled={value >= MAX_PEOPLE}
          accessibilityRole="button"
          accessibilityLabel="More people"
          style={({ pressed }) => [styles.round, (pressed || value >= MAX_PEOPLE) && styles.dim]}
        >
          <Text style={styles.sign}>+</Text>
        </Pressable>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  row: { minHeight: 48, flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center' },
  title: { color: colors.text, fontSize: 17 },
  hint: { color: colors.muted, fontSize: 13 },
  controls: { flexDirection: 'row', alignItems: 'center', gap: 12 },
  round: {
    width: 48,
    height: 48,
    borderRadius: 24,
    borderWidth: 1.5,
    borderColor: colors.border,
    alignItems: 'center',
    justifyContent: 'center',
  },
  dim: { opacity: 0.5 },
  sign: { color: colors.text, fontSize: 28, lineHeight: 32 },
  input: {
    width: 64,
    height: 48,
    borderWidth: 1.5,
    borderColor: colors.border,
    borderRadius: 12,
    color: colors.text,
    textAlign: 'center',
    fontSize: 24,
    fontWeight: '700',
    padding: 0,
  },
});
