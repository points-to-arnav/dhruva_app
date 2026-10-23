import { router } from 'expo-router';
import { useEffect } from 'react';
import { BackHandler, Pressable, StyleSheet, Text, View } from 'react-native';

import { CheckIcon } from '../components/CheckIcon';
import { DongleStatus } from '../components/DongleStatus';
import { RequestCard } from '../components/RequestCard';
import { Screen } from '../components/Screen';
import { Timeline, type Step } from '../components/Timeline';
import { describeRequest, formatTime } from '../state/sos';
import { useSos } from '../state/SosContext';
import { colors } from '../theme';

export default function Status() {
  const { phase, sentRequest, requestId, network, deliveredAt, sendError, isMock, sendSos, reset } = useSos();
  const delivered = phase === 'delivered';

  // An active SOS has no way back; the dongle keeps it going.
  useEffect(() => {
    const sub = BackHandler.addEventListener('hardwareBackPress', () => true);
    return () => sub.remove();
  }, []);

  const startOver = () => {
    reset();
    router.replace('/');
  };

  const steps: Step[] = [
    {
      title: 'Saved on your dongle',
      detail: requestId ?? (sendError ? 'Not saved' : 'Saving…'),
      state: requestId ? 'done' : 'active',
    },
    {
      title: network ? 'Reached the network' : 'Reaching the network',
      detail: network ? `Via ${network.via} · ${network.hops} ${network.hops === 1 ? 'hop' : 'hops'}` : 'Looking for a nearby node',
      state: network ? 'done' : requestId ? 'active' : 'pending',
    },
    {
      title: delivered ? 'Rescue team received it' : 'Rescue team receives it',
      detail: deliveredAt ? `Confirmed at ${formatTime(deliveredAt)}` : 'Not yet',
      state: delivered ? 'done' : 'pending',
    },
  ];

  return (
    <Screen>
      <DongleStatus />
      {/* Long-press the banner to start over: a demo control, mock mode only. */}
      <Pressable onLongPress={isMock ? startOver : undefined} delayLongPress={1000} accessibilityRole="header">
        {delivered ? (
          <View style={[styles.banner, styles.bannerDelivered]}>
            <CheckIcon size={20} color={colors.text} />
            <Text style={styles.bannerText}>SOS DELIVERED</Text>
          </View>
        ) : (
          <View style={[styles.banner, styles.bannerActive]}>
            <Text style={[styles.bannerText, styles.bannerTextActive]}>SOS REQUEST ACTIVE</Text>
          </View>
        )}
      </Pressable>

      <Text style={styles.heading}>
        {delivered ? 'Rescue team has your request' : requestId ? 'Saved on your dongle' : 'Saving to your dongle'}
      </Text>
      <Text style={styles.body}>
        {delivered
          ? 'Stay where you are if it is safe, and keep the dongle on.'
          : 'You do not need to press anything again. The dongle keeps trying until the network has it.'}
      </Text>

      {sendError && !requestId ? (
        <View style={styles.error} accessibilityRole="alert">
          <Text style={styles.errorTitle}>Could not reach the dongle</Text>
          <Text style={styles.errorBody}>{sendError}. Check the cable, then try again.</Text>
          <Pressable onPress={sendSos} accessibilityRole="button" style={styles.retry}>
            <Text style={styles.retryText}>Try again</Text>
          </Pressable>
        </View>
      ) : null}

      <View style={styles.timeline}>
        <Timeline steps={steps} />
      </View>

      {sentRequest ? (
        <View style={styles.request}>
          <RequestCard summary={describeRequest(sentRequest)} />
        </View>
      ) : null}
    </Screen>
  );
}

const styles = StyleSheet.create({
  banner: {
    marginTop: 16,
    height: 52,
    borderRadius: 12,
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
    gap: 10,
  },
  bannerActive: { backgroundColor: colors.red },
  bannerDelivered: { borderWidth: 2, borderColor: colors.text },
  bannerText: { color: colors.text, fontSize: 17, fontWeight: '800', letterSpacing: 1.4 },
  bannerTextActive: { color: colors.white },
  heading: { color: colors.text, marginTop: 28, fontSize: 30, fontWeight: '700', lineHeight: 36 },
  body: { color: colors.soft, marginTop: 10, fontSize: 17, lineHeight: 24 },
  error: { marginTop: 20, borderWidth: 1.5, borderColor: colors.text, borderRadius: 14, padding: 14, gap: 4 },
  errorTitle: { color: colors.text, fontSize: 17, fontWeight: '700' },
  errorBody: { color: colors.soft, fontSize: 15, lineHeight: 20 },
  retry: {
    marginTop: 10,
    height: 48,
    borderRadius: 24,
    backgroundColor: colors.text,
    alignItems: 'center',
    justifyContent: 'center',
  },
  retryText: { color: colors.bg, fontSize: 17, fontWeight: '700' },
  timeline: { marginTop: 32 },
  request: { marginTop: 'auto', paddingTop: 24 },
});
