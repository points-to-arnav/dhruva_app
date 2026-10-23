import { router } from 'expo-router';
import { useEffect, useRef, useState } from 'react';
import { Pressable, StyleSheet, Text, View } from 'react-native';
import Svg, { Circle } from 'react-native-svg';

import { DongleStatus } from '../components/DongleStatus';
import { Screen } from '../components/Screen';
import { describeRequest } from '../state/sos';
import { useSos } from '../state/SosContext';
import { colors } from '../theme';

const COUNTDOWN_MS = 5000;
const RING_RADIUS = 143;
const RING_LENGTH = 2 * Math.PI * RING_RADIUS;

export default function Countdown() {
  const { connected, needs, people, sendSos } = useSos();
  const [remainingMs, setRemainingMs] = useState(COUNTDOWN_MS);
  const fired = useRef(false);

  useEffect(() => {
    const startedAt = Date.now();
    const timer = setInterval(() => {
      const left = Math.max(0, COUNTDOWN_MS - (Date.now() - startedAt));
      setRemainingMs(left);
      if (left === 0 && !fired.current) {
        fired.current = true;
        clearInterval(timer);
        sendSos();
        router.replace('/status');
      }
    }, 50);
    return () => clearInterval(timer);
  }, [sendSos]);

  // Unplugged during the countdown: there is nothing to send through.
  useEffect(() => {
    if (!connected && !fired.current && router.canGoBack()) router.back();
  }, [connected]);

  const seconds = Math.ceil(remainingMs / 1000);
  const elapsed = (COUNTDOWN_MS - remainingMs) / COUNTDOWN_MS;

  return (
    <Screen>
      <DongleStatus />
      <View style={styles.dial}>
        <Svg width={254} height={254} viewBox="0 0 292 292" style={StyleSheet.absoluteFill} accessible={false}>
          <Circle cx={146} cy={146} r={RING_RADIUS} fill="none" stroke={colors.track} strokeWidth={4} />
          <Circle
            cx={146}
            cy={146}
            r={RING_RADIUS}
            fill="none"
            stroke={colors.text}
            strokeWidth={4}
            strokeLinecap="round"
            strokeDasharray={RING_LENGTH}
            strokeDashoffset={RING_LENGTH * elapsed}
            transform="rotate(-90 146 146)"
          />
        </Svg>
        <View style={styles.number}>
          <Text style={styles.numberText}>{seconds}</Text>
        </View>
      </View>
      <Text style={styles.title} accessibilityLiveRegion="polite">
        Sending SOS in {seconds} {seconds === 1 ? 'second' : 'seconds'}
      </Text>
      <Text style={styles.summary}>{describeRequest({ needs, people })}</Text>
      <Text style={styles.hint}>Pressed by mistake? Tap Cancel.</Text>
      <Pressable
        onPress={() => router.back()}
        accessibilityRole="button"
        style={({ pressed }) => [styles.cancel, pressed && styles.cancelPressed]}
      >
        <Text style={styles.cancelText}>Cancel</Text>
      </Pressable>
    </Screen>
  );
}

const styles = StyleSheet.create({
  dial: { width: 254, height: 254, marginTop: 44, alignSelf: 'center' },
  number: {
    position: 'absolute',
    left: 13,
    top: 13,
    width: 228,
    height: 228,
    borderRadius: 114,
    backgroundColor: colors.red,
    alignItems: 'center',
    justifyContent: 'center',
  },
  numberText: { color: colors.white, fontSize: 110, fontWeight: '800', lineHeight: 120 },
  title: { color: colors.text, marginTop: 36, textAlign: 'center', fontSize: 28, fontWeight: '700', lineHeight: 34 },
  summary: { color: colors.soft, marginTop: 12, textAlign: 'center', fontSize: 17, lineHeight: 24 },
  hint: { color: colors.muted, marginTop: 'auto', paddingTop: 24, paddingBottom: 12, textAlign: 'center', fontSize: 15 },
  cancel: {
    height: 72,
    borderRadius: 36,
    borderWidth: 2,
    borderColor: colors.text,
    alignItems: 'center',
    justifyContent: 'center',
  },
  cancelPressed: { backgroundColor: colors.divider },
  cancelText: { color: colors.text, fontSize: 24, fontWeight: '700' },
});
