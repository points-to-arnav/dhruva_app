import { Pressable, StyleSheet, Text, View } from 'react-native';

import { NEEDS, type Need } from '../state/sos';
import { colors } from '../theme';
import { CheckIcon } from './CheckIcon';

type Props = { selected: Need[]; onToggle: (need: Need) => void };

export function NeedChips({ selected, onToggle }: Props) {
  return (
    <View>
      <Text style={styles.prompt}>What is happening? Pick all that apply</Text>
      <View style={styles.wrap}>
        {NEEDS.map((need) => {
          const on = selected.includes(need);
          return (
            <Pressable
              key={need}
              onPress={() => onToggle(need)}
              accessibilityRole="checkbox"
              accessibilityState={{ checked: on }}
              style={({ pressed }) => [styles.chip, on ? styles.chipOn : styles.chipOff, pressed && styles.pressed]}
            >
              {on ? <CheckIcon color={colors.bg} /> : null}
              <Text style={[styles.chipText, on ? styles.chipTextOn : styles.chipTextOff]}>{need}</Text>
            </Pressable>
          );
        })}
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  prompt: { color: colors.muted, fontSize: 15 },
  wrap: { flexDirection: 'row', flexWrap: 'wrap', gap: 8, marginTop: 8 },
  chip: {
    height: 48,
    borderRadius: 24,
    borderWidth: 1.5,
    flexDirection: 'row',
    alignItems: 'center',
    gap: 6,
  },
  chipOn: { backgroundColor: colors.text, borderColor: colors.text, paddingLeft: 14, paddingRight: 18 },
  chipOff: { backgroundColor: 'transparent', borderColor: colors.border, paddingHorizontal: 18 },
  chipText: { fontSize: 17 },
  chipTextOn: { color: colors.bg, fontWeight: '700' },
  chipTextOff: { color: colors.text },
  pressed: { opacity: 0.7 },
});
