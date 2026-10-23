import { useCallback, useEffect, useMemo, useReducer, useRef, useState } from 'react';

import { FAKE_STATUS_PROGRESS, FUNCTION_DELAY_XYZ } from '../config';
import { useBattery } from '../hooks/useBattery';
import { usePosition } from '../hooks/usePosition';
import { createLink, encodeSend, INFO_REQUEST, parseLine } from '../serial';
import { clampPeople } from './sos';
import { SosContext } from './useSos';

// Give a C3 that reset on port open a moment before asking for INFO.
const INFO_DELAY_MS = 300;

// Fake STATUS sequence (see FAKE_STATUS_PROGRESS): one step per random delay.
const FAKE_STEPS = [{ state: 'STORED' }, { state: 'SENT_TO_MESH', nextHop: '0x0011' }, { state: 'DELIVERED' }];

function fakeStepDelay() {
  const { min, max } = FUNCTION_DELAY_XYZ;
  return min + Math.random() * (max - min);
}

const emptyMessage = { id: null, state: null, nextHop: null, deliveredAt: null };

const initialState = {
  connected: false,
  nodesInRange: null,
  needs: [],
  people: 1,
  active: false,
  sentRequest: null,
  message: emptyMessage,
  sendError: null,
  connectError: null,
};

function reducer(state, action) {
  switch (action.type) {
    case 'connection':
      return action.connected
        ? { ...state, connected: true, connectError: null }
        : { ...state, connected: false, nodesInRange: null };
    case 'dongle': {
      const event = action.event;
      if (event.type === 'info') return { ...state, nodesInRange: event.nodesInRange };
      // The first STATUS after a SEND names our message; later ones must match
      // it. Others are older messages still stored on the dongle.
      const { message } = state;
      if (!state.active || (message.id !== null && message.id !== event.messageId)) return state;
      return {
        ...state,
        message: {
          id: event.messageId,
          state: event.state,
          nextHop: event.nextHop ?? message.nextHop,
          deliveredAt: event.state === 'DELIVERED' ? (message.deliveredAt ?? new Date()) : message.deliveredAt,
        },
      };
    }
    case 'connectFailed':
      return { ...state, connectError: action.message };
    case 'toggleNeed':
      return {
        ...state,
        needs: state.needs.includes(action.need)
          ? state.needs.filter((n) => n !== action.need)
          : [...state.needs, action.need],
      };
    case 'setPeople':
      return { ...state, people: clampPeople(action.people) };
    case 'sendStarted':
      return {
        ...state,
        active: true,
        sentRequest: { needs: state.needs, people: state.people },
        message: emptyMessage,
        sendError: null,
      };
    case 'sendFailed':
      return { ...state, sendError: action.message };
    case 'reset':
      return { ...initialState, connected: state.connected, nodesInRange: state.nodesInRange };
    default:
      return state;
  }
}

export function SosProvider({ children }) {
  const [link] = useState(createLink);
  const [state, dispatch] = useReducer(reducer, initialState);
  const { status: location, position } = usePosition();
  const battery = useBattery();

  // Latest picks and position for sendSos, which stays stable so the
  // countdown timer doesn't restart on every GPS update.
  const latest = useRef({ state, position });
  useEffect(() => {
    latest.current = { state, position };
  });

  const fakeProgress = FAKE_STATUS_PROGRESS && !link.mock;
  const fakeTimers = useRef([]);
  const fakeCount = useRef(41);
  const stopFakeProgress = useCallback(() => {
    fakeTimers.current.forEach(clearTimeout);
    fakeTimers.current = [];
  }, []);

  // Walks the status screen through FAKE_STEPS as if the dongle had replied.
  const startFakeProgress = useCallback(() => {
    stopFakeProgress();
    fakeCount.current += 1;
    const messageId = `2001-${String(fakeCount.current).padStart(5, '0')}`;
    let at = 0;
    FAKE_STEPS.forEach(({ state: fakeState, nextHop = null }) => {
      at += fakeStepDelay();
      const event = { type: 'status', messageId, state: fakeState, nextHop };
      fakeTimers.current.push(setTimeout(() => dispatch({ type: 'dongle', event }), at));
    });
  }, [stopFakeProgress]);

  useEffect(() => stopFakeProgress, [stopFakeProgress]);

  useEffect(() => {
    let infoTimer;
    const unsubscribe = link.subscribe((event) => {
      if (event.type === 'connection') {
        dispatch({ type: 'connection', connected: event.connected });
        if (event.connected) infoTimer = setTimeout(() => link.writeLine(INFO_REQUEST).catch(() => {}), INFO_DELAY_MS);
        return;
      }
      const parsed = parseLine(event.line);
      // While faking, the dongle's own STATUS lines would fight the fake ones.
      if (parsed && !(fakeProgress && parsed.type === 'status')) dispatch({ type: 'dongle', event: parsed });
    });
    link.start();
    return () => {
      clearTimeout(infoTimer);
      unsubscribe();
      link.stop();
    };
  }, [link, fakeProgress]);

  const connect = useCallback(() => {
    link.connect().catch((error) => {
      // NotFoundError: the user closed the picker without choosing.
      if (error?.name !== 'NotFoundError') dispatch({ type: 'connectFailed', message: error?.message ?? String(error) });
    });
  }, [link]);

  const sendSos = useCallback(() => {
    const { state: current, position: where } = latest.current;
    dispatch({ type: 'sendStarted' });
    const line = encodeSend({ people: current.people, needs: current.needs, position: where, time: new Date() });
    stopFakeProgress();
    link
      .writeLine(line)
      .then(() => {
        if (fakeProgress) startFakeProgress();
      })
      .catch((error) => {
        dispatch({ type: 'sendFailed', message: error?.message ?? String(error) });
      });
  }, [link, fakeProgress, startFakeProgress, stopFakeProgress]);

  const value = useMemo(
    () => ({
      ...state,
      battery,
      location,
      supported: link.supported,
      unsupportedReason: link.unsupportedReason,
      isMock: Boolean(link.mock),
      connect,
      sendSos,
      toggleNeed: (need) => dispatch({ type: 'toggleNeed', need }),
      setPeople: (people) => dispatch({ type: 'setPeople', people }),
      reset: () => {
        stopFakeProgress();
        dispatch({ type: 'reset' });
      },
      toggleMockConnection: () => link.mock?.toggleConnection(),
      toggleMockNodes: () => link.mock?.toggleNodes(),
    }),
    [state, battery, location, link, connect, sendSos, stopFakeProgress],
  );

  return <SosContext.Provider value={value}>{children}</SosContext.Provider>;
}
