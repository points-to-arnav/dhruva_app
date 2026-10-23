import { router } from 'expo-router';
import { Pressable, StyleSheet, Text, View } from 'react-native';

import { DongleStatus } from '../components/DongleStatus';
import { NeedChips } from '../components/NeedChips';
import { PeopleStepper } from '../components/PeopleStepper';
import { Screen } from '../components/Screen';
import { useLocationStatus, type LocationStatus } from '../hooks/useLocationStatus';
import { useSos } from '../state/SosContext';
import { colors } from '../theme';

export default function Home() {
  const { connected } = useSos();
  const location = useLocationStatus();

  return <Screen>{connected ? <Ready location={location.status} /> : <NoDongle />}</Screen>;
}

function Ready({ location }: { location: LocationStatus }) {
  const { nodesInRange, needs, people, toggleNeed, setPeople } = useSos();

  const nodes = nodesInRange === null ? null : nodesInRange === 1 ? '1 node in range' : `${nodesInRange} nodes in range`;
  const locationText = location === 'found' ? 'Location found' : location === 'searching' ? 'Finding location…' : null;
  const meta = [nodes, locationText].filter(Boolean).join(' · ');

  return (
    <>
      <DongleStatus />
      <SosButton onPress={() => router.push('/countdown')} />
      <Text style={styles.instruction}>Press once. It is saved and sent automatically.</Text>
      {meta ? <Text style={styles.meta}>{meta}</Text> : null}
      {location === 'unavailable' ? <LocationWarning /> : null}
      <View style={[styles.divider, location === 'unavailable' && styles.dividerTight]} />
      <NeedChips selected={needs} onToggle={toggleNeed} />
      <View style={styles.people}>
        <PeopleStepper value={people} onChange={setPeople} />
      </View>
    </>
  );
}

function LocationWarning() {
  return (
    <View style={styles.warning} accessibilityRole="alert">
      <View style={styles.warningIcon}>
        <Text style={styles.warningMark}>!</Text>
      </View>
      <View style={styles.warningText}>
        <Text style={styles.warningTitle}>Location not found</Text>
        <Text style={styles.warningBody}>Rescuers will not know where you are. Try near a window or outside.</Text>
      </View>
    </View>
  );
}

function NoDongle() {
  const steps = [
    'Connect the dongle to your phone with the USB-C cable.',
    'Tap Allow when your phone asks for USB access.',
    'This screen updates by itself.',
  ];
  return (
    <>
      <DongleStatus />
      <SosButton disabled />
      <Text style={styles.noDongleTitle}>Plug in the dongle to send SOS</Text>
      <View style={styles.steps}>
        {steps.map((step, i) => (
          <View key={step} style={styles.step}>
            <View style={styles.stepNumber}>
              <Text style={styles.stepNumberText}>{i + 1}</Text>
            </View>
            <Text style={styles.stepText}>{step}</Text>
          </View>
        ))}
      </View>
      <Text style={styles.footer}>Still nothing? Unplug the dongle and plug it in again.</Text>
    </>
  );
}

function SosButton({ onPress, disabled }: { onPress?: () => void; disabled?: boolean }) {
  return (
    <View style={[styles.sosRing, disabled && styles.sosRingDisabled]}>
      <Pressable
        onPress={onPress}
        disabled={disabled}
        accessibilityRole="button"
        accessibilityLabel={disabled ? 'Send SOS, unavailable until the dongle is connected' : 'Send SOS'}
        accessibilityState={{ disabled }}
        style={({ pressed }) => [styles.sos, disabled && styles.sosDisabled, pressed && styles.sosPressed]}
      >
        <Text style={[styles.sosText, disabled && styles.sosTextDisabled]}>SOS</Text>
      </Pressable>
    </View>
  );
}

const styles = StyleSheet.create({
  sosRing: {
    width: 246,
    height: 246,
    marginTop: 12,
    alignSelf: 'center',
    borderRadius: 123,
    borderWidth: 2,
    borderColor: colors.redRing,
    alignItems: 'center',
    justifyContent: 'center',
  },
  sosRingDisabled: { borderColor: colors.track },
  sos: {
    width: 220,
    height: 220,
    borderRadius: 110,
    backgroundColor: colors.red,
    alignItems: 'center',
    justifyContent: 'center',
  },
  sosDisabled: { backgroundColor: colors.disabledBg },
  sosPressed: { transform: [{ scale: 0.97 }] },
  sosText: { color: colors.white, fontSize: 65, fontWeight: '800', letterSpacing: 2.6 },
  sosTextDisabled: { color: colors.disabledText },
  instruction: { color: colors.text, textAlign: 'center', marginTop: 14, fontSize: 17, lineHeight: 24 },
  meta: { color: colors.muted, textAlign: 'center', marginTop: 4, fontSize: 15 },
  divider: { height: 1, backgroundColor: colors.divider, marginTop: 18, marginBottom: 14 },
  dividerTight: { marginTop: 14, marginBottom: 12 },
  people: { marginTop: 12 },
  warning: {
    marginTop: 10,
    flexDirection: 'row',
    gap: 12,
    alignItems: 'flex-start',
    borderWidth: 1.5,
    borderColor: colors.text,
    borderRadius: 14,
    paddingVertical: 12,
    paddingHorizontal: 14,
  },
  warningIcon: {
    width: 28,
    height: 28,
    borderRadius: 14,
    borderWidth: 2,
    borderColor: colors.text,
    alignItems: 'center',
    justifyContent: 'center',
  },
  warningMark: { color: colors.text, fontSize: 16, fontWeight: '800' },
  warningText: { flex: 1 },
  warningTitle: { color: colors.text, fontSize: 17, fontWeight: '700', lineHeight: 22 },
  warningBody: { color: colors.soft, fontSize: 15, lineHeight: 20, marginTop: 2 },
  noDongleTitle: { color: colors.text, textAlign: 'center', marginTop: 28, fontSize: 26, fontWeight: '700', lineHeight: 32 },
  steps: { marginTop: 24, gap: 16 },
  step: { flexDirection: 'row', gap: 14, alignItems: 'flex-start' },
  stepNumber: {
    width: 32,
    height: 32,
    borderRadius: 16,
    borderWidth: 2,
    borderColor: colors.text,
    alignItems: 'center',
    justifyContent: 'center',
  },
  stepNumberText: { color: colors.text, fontSize: 16, fontWeight: '700' },
  stepText: { flex: 1, color: colors.text, fontSize: 17, lineHeight: 24, paddingTop: 4 },
  footer: { marginTop: 'auto', paddingTop: 24, color: colors.muted, textAlign: 'center', fontSize: 15 },
});
