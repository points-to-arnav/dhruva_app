import { useCallback, useEffect, useMemo, useReducer, useRef, useState } from 'react';

import { useBattery } from '../hooks/useBattery';
import { usePosition } from '../hooks/usePosition';
import { createLink, encodeSend, INFO_REQUEST, parseLine } from '../serial';
import { clampPeople } from './sos';
import { SosContext } from './useSos';

// Give a C3 that reset on port open a moment before asking for INFO.
const INFO_DELAY_MS = 300;

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

  useEffect(() => {
    let infoTimer;
    const unsubscribe = link.subscribe((event) => {
      if (event.type === 'connection') {
        dispatch({ type: 'connection', connected: event.connected });
        if (event.connected) infoTimer = setTimeout(() => link.writeLine(INFO_REQUEST).catch(() => {}), INFO_DELAY_MS);
        return;
      }
      const parsed = parseLine(event.line);
      if (parsed) dispatch({ type: 'dongle', event: parsed });
    });
    link.start();
    return () => {
      clearTimeout(infoTimer);
      unsubscribe();
      link.stop();
    };
  }, [link]);

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
    link.writeLine(line).catch((error) => {
      dispatch({ type: 'sendFailed', message: error?.message ?? String(error) });
    });
  }, [link]);

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
      reset: () => dispatch({ type: 'reset' }),
      toggleMockConnection: () => link.mock?.toggleConnection(),
      toggleMockNodes: () => link.mock?.toggleNodes(),
    }),
    [state, battery, location, link, connect, sendSos],
  );

  return <SosContext.Provider value={value}>{children}</SosContext.Provider>;
}
