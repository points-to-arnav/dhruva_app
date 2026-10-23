import { useEffect, useRef, useState } from 'react';

import { DongleStatus } from '../components/DongleStatus';
import { Screen } from '../components/Screen';
import { describeRequest } from '../state/sos';
import { useSos } from '../state/useSos';

const COUNTDOWN_MS = 5000;
const RING_RADIUS = 143;
const RING_LENGTH = 2 * Math.PI * RING_RADIUS;

export function Countdown({ onCancel, onSent }) {
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
        onSent();
      }
    }, 50);
    return () => clearInterval(timer);
  }, [sendSos, onSent]);

  // Unplugged during the countdown: there is nothing to send through.
  useEffect(() => {
    if (!connected && !fired.current) onCancel();
  }, [connected, onCancel]);

  const seconds = Math.ceil(remainingMs / 1000);
  const elapsed = (COUNTDOWN_MS - remainingMs) / COUNTDOWN_MS;

  return (
    <Screen>
      <DongleStatus />
      <div className="relative mx-auto mt-11 size-[254px]">
        <svg width="254" height="254" viewBox="0 0 292 292" aria-hidden="true" className="absolute inset-0">
          <circle cx="146" cy="146" r={RING_RADIUS} fill="none" className="stroke-track" strokeWidth="4" />
          <circle
            cx="146"
            cy="146"
            r={RING_RADIUS}
            fill="none"
            className="stroke-ink"
            strokeWidth="4"
            strokeLinecap="round"
            strokeDasharray={RING_LENGTH}
            strokeDashoffset={RING_LENGTH * elapsed}
            transform="rotate(-90 146 146)"
          />
        </svg>
        <div className="absolute top-[13px] left-[13px] flex size-[228px] items-center justify-center rounded-full bg-sos text-[110px] leading-none font-extrabold text-white">
          {seconds}
        </div>
      </div>
      <p aria-live="polite" className="mt-9 text-center text-[28px] leading-[34px] font-bold">
        Sending SOS in {seconds} {seconds === 1 ? 'second' : 'seconds'}
      </p>
      <p className="mt-3 text-center text-[17px] leading-6 text-soft">{describeRequest({ needs, people })}</p>
      <p className="mt-auto pt-6 pb-3 text-center text-[15px] text-muted">Pressed by mistake? Tap Cancel.</p>
      <button
        type="button"
        onClick={onCancel}
        className="h-[72px] cursor-pointer rounded-full border-2 border-ink text-2xl font-bold active:bg-divider"
      >
        Cancel
      </button>
    </Screen>
  );
}
