import { StyleSheet, Text, View } from 'react-native';

import { colors } from '../theme';
import { CheckIcon } from './CheckIcon';

export type StepState = 'done' | 'active' | 'pending';
export type Step = { title: string; detail: string; state: StepState };

export function Timeline({ steps }: { steps: Step[] }) {
  const allDone = steps.every((s) => s.state === 'done');
  return (
    <View style={styles.wrap}>
      <View style={[styles.line, { backgroundColor: allDone ? colors.text : colors.border }]} />
      <View style={styles.list}>
        {steps.map((step) => (
          <View key={step.title} style={styles.row} accessible accessibilityLabel={`${step.title}. ${step.detail}`}>
            <StepIcon state={step.state} />
            <View style={styles.text}>
              <Text style={[styles.title, step.state === 'pending' && styles.pendingText]}>{step.title}</Text>
              <Text style={[styles.detail, step.state === 'pending' && styles.pendingText]}>{step.detail}</Text>
            </View>
          </View>
        ))}
      </View>
    </View>
  );
}

function StepIcon({ state }: { state: StepState }) {
  if (state === 'done') {
    return (
      <View style={[styles.icon, styles.iconDone]}>
        <CheckIcon size={20} color={colors.bg} />
      </View>
    );
  }
  if (state === 'active') {
    return (
      <View style={[styles.icon, styles.iconActive]}>
        <View style={styles.activeDot} />
      </View>
    );
  }
  return <View style={[styles.icon, styles.iconPending]} />;
}

const styles = StyleSheet.create({
  wrap: { position: 'relative' },
  line: { position: 'absolute', left: 17, top: 18, bottom: 26, width: 2 },
  list: { gap: 24 },
  row: { flexDirection: 'row', gap: 16, alignItems: 'flex-start' },
  icon: { width: 36, height: 36, borderRadius: 18, alignItems: 'center', justifyContent: 'center' },
  iconDone: { backgroundColor: colors.text },
  iconActive: { borderWidth: 2.5, borderColor: colors.text, backgroundColor: colors.bg },
  iconPending: { borderWidth: 2.5, borderColor: colors.border, backgroundColor: colors.bg },
  activeDot: { width: 12, height: 12, borderRadius: 6, backgroundColor: colors.text },
  text: { flex: 1 },
  title: { color: colors.text, fontSize: 19, fontWeight: '600', lineHeight: 24 },
  detail: { color: colors.muted, fontSize: 15, marginTop: 2 },
  pendingText: { color: colors.faint },
});
