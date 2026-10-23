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
  const { sentRequest, message, sendError, isMock, sendSos, reset } = useSos();
  const { state } = message;
  const saved = state !== null && state !== 'QUEUED';
  const delivered = state === 'DELIVERED';
  const failed = state === 'FAILED';
  const waiting = state === 'WAITING';
  const reached = delivered || state === 'SENT_TO_MESH' || message.nextHop !== null;

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
      detail: saved && message.id ? message.id : sendError ? 'Not saved' : 'Saving…',
      state: saved ? 'done' : 'active',
    },
    reached
      ? {
          title: 'Reached the network',
          detail: message.nextHop ? `Via ${message.nextHop}` : 'Handed to a nearby node',
          state: 'done',
        }
      : {
          title: 'Reaching the network',
          detail: waiting
            ? 'No node in range yet. Will keep trying.'
            : state === 'TRANSMITTING'
              ? 'Sending to a nearby node'
              : 'Looking for a nearby node',
          state: saved ? 'active' : 'pending',
        },
    {
      title: delivered ? 'Rescue team received it' : 'Rescue team receives it',
      detail: message.deliveredAt ? `Confirmed at ${formatTime(message.deliveredAt)}` : failed ? 'Did not arrive' : 'Not yet',
      state: delivered ? 'done' : 'pending',
    },
  ];

  const [heading, body] = delivered
    ? ['Rescue team has your request', 'Stay where you are if it is safe, and keep the dongle on.']
    : failed
      ? ['Your SOS did not reach the rescue team', 'It ran out of hops on the way. Send it again to try another route.']
      : waiting
        ? [
            'Saved, waiting for the network',
            'No rescue node is in range yet. The dongle keeps your message and sends it as soon as one is. Move outside or higher if you can.',
          ]
        : [
            saved ? 'Saved on your dongle' : 'Saving to your dongle',
            'You do not need to press anything again. The dongle keeps trying until the network has it.',
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
            <Text style={[styles.bannerText, styles.bannerTextActive]}>
              {failed ? 'SOS NOT DELIVERED' : 'SOS REQUEST ACTIVE'}
            </Text>
          </View>
        )}
      </Pressable>

      <Text style={styles.heading}>{heading}</Text>
      <Text style={styles.body}>{body}</Text>

      {failed ? (
        <Pressable onPress={sendSos} accessibilityRole="button" style={styles.retry}>
          <Text style={styles.retryText}>Send again</Text>
        </Pressable>
      ) : null}

      {sendError && !saved ? (
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
