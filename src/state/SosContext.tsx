import { createContext, useCallback, useContext, useEffect, useMemo, useReducer, useRef, type ReactNode } from 'react';

import { usePosition, type LocationStatus } from '../hooks/usePosition';
import {
  createLink,
  encodeSend,
  INFO_REQUEST,
  parseLine,
  type DongleEvent,
  type MessageState,
  type SerialLink,
} from '../serial';
import { clampPeople, type Need } from './sos';

export type Message = {
  id: string | null;
  state: MessageState | null;
  nextHop: string | null;
  deliveredAt: Date | null;
};

type State = {
  connected: boolean;
  battery: number | null;
  nodesInRange: number | null;
  needs: Need[];
  people: number;
  active: boolean;
  sentRequest: { needs: Need[]; people: number } | null;
  message: Message;
  sendError: string | null;
};

type Action =
  | { type: 'connection'; connected: boolean }
  | { type: 'dongle'; event: DongleEvent }
  | { type: 'toggleNeed'; need: Need }
  | { type: 'setPeople'; people: number }
  | { type: 'sendStarted' }
  | { type: 'sendFailed'; message: string }
  | { type: 'reset' };

const emptyMessage: Message = { id: null, state: null, nextHop: null, deliveredAt: null };

const initialState: State = {
  connected: false,
  battery: null,
  nodesInRange: null,
  needs: [],
  people: 1,
  active: false,
  sentRequest: null,
  message: emptyMessage,
  sendError: null,
};

function reducer(state: State, action: Action): State {
  switch (action.type) {
    case 'connection':
      return action.connected
        ? { ...state, connected: true }
        : { ...state, connected: false, battery: null, nodesInRange: null };
    case 'dongle': {
      const event = action.event;
      if (event.type === 'info') {
        return { ...state, battery: event.battery, nodesInRange: event.nodesInRange };
      }
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
      return {
        ...initialState,
        connected: state.connected,
        battery: state.battery,
        nodesInRange: state.nodesInRange,
      };
  }
}

type SosContextValue = State & {
  location: LocationStatus;
  isMock: boolean;
  toggleNeed: (need: Need) => void;
  setPeople: (people: number) => void;
  sendSos: () => void;
  reset: () => void;
  toggleMockConnection: () => void;
  toggleMockNodes: () => void;
};

const SosContext = createContext<SosContextValue | null>(null);

export function SosProvider({ children }: { children: ReactNode }) {
  const linkRef = useRef<SerialLink | null>(null);
  if (!linkRef.current) linkRef.current = createLink();
  const link = linkRef.current;

  const [state, dispatch] = useReducer(reducer, initialState);
  const stateRef = useRef(state);
  stateRef.current = state;

  const { status: location, position } = usePosition();
  const positionRef = useRef(position);
  positionRef.current = position;

  useEffect(() => {
    const unsubscribe = link.subscribe((event) => {
      if (event.type === 'connection') {
        dispatch({ type: 'connection', connected: event.connected });
        if (event.connected) link.writeLine(INFO_REQUEST).catch(() => {});
        return;
      }
      const parsed = parseLine(event.line);
      if (parsed) dispatch({ type: 'dongle', event: parsed });
    });
    link.start();
    return () => {
      unsubscribe();
      link.stop();
    };
  }, [link]);

  const sendSos = useCallback(() => {
    const { needs, people } = stateRef.current;
    dispatch({ type: 'sendStarted' });
    const line = encodeSend({ people, needs, position: positionRef.current, time: new Date() });
    link.writeLine(line).catch((error: unknown) => {
      dispatch({ type: 'sendFailed', message: error instanceof Error ? error.message : String(error) });
    });
  }, [link]);

  const value = useMemo<SosContextValue>(
    () => ({
      ...state,
      location,
      isMock: !!link.mock,
      toggleNeed: (need) => dispatch({ type: 'toggleNeed', need }),
      setPeople: (people) => dispatch({ type: 'setPeople', people }),
      sendSos,
      reset: () => dispatch({ type: 'reset' }),
      toggleMockConnection: () => link.mock?.toggleConnection(),
      toggleMockNodes: () => link.mock?.toggleNodes(),
    }),
    [state, location, link, sendSos],
  );

  return <SosContext.Provider value={value}>{children}</SosContext.Provider>;
}

export function useSos() {
  const value = useContext(SosContext);
  if (!value) throw new Error('useSos must be used inside SosProvider');
  return value;
}
