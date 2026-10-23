import { createContext, useCallback, useContext, useEffect, useMemo, useReducer, useRef, type ReactNode } from 'react';

import { buildSosPacket, createTransport, type DongleEvent, type DongleTransport } from '../serial';
import { MockTransport } from '../serial/mockTransport';
import { clampPeople, type Need } from './sos';

export type Phase = 'idle' | 'sending' | 'delivered';

type State = {
  connected: boolean;
  battery: number | null;
  nodesInRange: number | null;
  needs: Need[];
  people: number;
  phase: Phase;
  sentRequest: { needs: Need[]; people: number } | null;
  requestId: string | null;
  network: { via: string; hops: number } | null;
  deliveredAt: Date | null;
  sendError: string | null;
};

type Action =
  | { type: 'dongle'; event: DongleEvent }
  | { type: 'toggleNeed'; need: Need }
  | { type: 'setPeople'; people: number }
  | { type: 'sendStarted' }
  | { type: 'sendFailed'; message: string }
  | { type: 'reset' };

const initialState: State = {
  connected: false,
  battery: null,
  nodesInRange: null,
  needs: [],
  people: 1,
  phase: 'idle',
  sentRequest: null,
  requestId: null,
  network: null,
  deliveredAt: null,
  sendError: null,
};

function reducer(state: State, action: Action): State {
  switch (action.type) {
    case 'dongle': {
      const event = action.event;
      switch (event.type) {
        case 'connection':
          return event.connected
            ? { ...state, connected: true }
            : { ...state, connected: false, battery: null, nodesInRange: null };
        case 'status':
          return { ...state, battery: event.battery, nodesInRange: event.nodesInRange };
        case 'saved':
          return { ...state, requestId: event.requestId };
        case 'network':
          return { ...state, network: { via: event.via, hops: event.hops } };
        case 'delivered':
          return { ...state, phase: 'delivered', deliveredAt: event.at };
      }
      return state;
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
        phase: 'sending',
        sentRequest: { needs: state.needs, people: state.people },
        requestId: null,
        network: null,
        deliveredAt: null,
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
  isMock: boolean;
  toggleNeed: (need: Need) => void;
  setPeople: (people: number) => void;
  sendSos: () => void;
  reset: () => void;
  toggleMockConnection: () => void;
};

const SosContext = createContext<SosContextValue | null>(null);

export function SosProvider({ children }: { children: ReactNode }) {
  const transportRef = useRef<DongleTransport | null>(null);
  if (!transportRef.current) transportRef.current = createTransport();
  const transport = transportRef.current;

  const [state, dispatch] = useReducer(reducer, initialState);
  const stateRef = useRef(state);
  stateRef.current = state;

  useEffect(() => {
    const unsubscribe = transport.subscribe((event) => dispatch({ type: 'dongle', event }));
    transport.start();
    return () => {
      unsubscribe();
      transport.stop();
    };
  }, [transport]);

  const sendSos = useCallback(() => {
    const { needs, people } = stateRef.current;
    dispatch({ type: 'sendStarted' });
    transport.send(buildSosPacket({ needs, people })).catch((error: unknown) => {
      dispatch({ type: 'sendFailed', message: error instanceof Error ? error.message : String(error) });
    });
  }, [transport]);

  const value = useMemo<SosContextValue>(
    () => ({
      ...state,
      isMock: transport.isMock,
      toggleNeed: (need) => dispatch({ type: 'toggleNeed', need }),
      setPeople: (people) => dispatch({ type: 'setPeople', people }),
      sendSos,
      reset: () => dispatch({ type: 'reset' }),
      toggleMockConnection: () => {
        if (transport instanceof MockTransport) transport.toggleConnection();
      },
    }),
    [state, transport, sendSos],
  );

  return <SosContext.Provider value={value}>{children}</SosContext.Provider>;
}

export function useSos() {
  const value = useContext(SosContext);
  if (!value) throw new Error('useSos must be used inside SosProvider');
  return value;
}
