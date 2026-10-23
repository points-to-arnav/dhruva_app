import { useCallback, useEffect, useRef, useState } from 'react';

import { Countdown } from './screens/Countdown';
import { Home } from './screens/Home';
import { Status } from './screens/Status';
import { SosProvider } from './state/SosContext';

export default function App() {
  return (
    <SosProvider>
      <Screens />
    </SosProvider>
  );
}

// home -> countdown -> status. Leaving home pushes one history entry, so the
// phone's back button cancels the countdown; on the status screen it does
// nothing, since an active SOS has no way back.
function Screens() {
  const [screen, setScreen] = useState('home');
  const screenRef = useRef(screen);
  useEffect(() => {
    screenRef.current = screen;
  });

  useEffect(() => {
    const onPop = () => {
      if (screenRef.current === 'status') window.history.pushState({ screen: 'status' }, '');
      else setScreen('home');
    };
    window.addEventListener('popstate', onPop);
    return () => window.removeEventListener('popstate', onPop);
  }, []);

  const openCountdown = useCallback(() => {
    window.history.pushState({ screen: 'countdown' }, '');
    setScreen('countdown');
  }, []);

  const goHome = useCallback(() => {
    if (screenRef.current === 'home') return;
    screenRef.current = 'home';
    setScreen('home');
    window.history.back();
  }, []);

  const showStatus = useCallback(() => {
    window.history.replaceState({ screen: 'status' }, '');
    setScreen('status');
  }, []);

  if (screen === 'countdown') return <Countdown onCancel={goHome} onSent={showStatus} />;
  if (screen === 'status') return <Status onStartOver={goHome} />;
  return <Home onSos={openCountdown} />;
}
