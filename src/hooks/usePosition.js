import { useEffect, useState } from 'react';

import { DEMO_POSITION } from '../config';

// Give up on "searching" after this long and warn the user instead.
const SEARCH_TIMEOUT_MS = 20000;

// status: 'demo' | 'searching' | 'found' | 'unavailable'
export function usePosition() {
  const [status, setStatus] = useState(() =>
    DEMO_POSITION ? 'demo' : navigator.geolocation ? 'searching' : 'unavailable',
  );
  const [position, setPosition] = useState(DEMO_POSITION);

  useEffect(() => {
    if (DEMO_POSITION || !navigator.geolocation) return;
    const timeout = setTimeout(() => setStatus((s) => (s === 'searching' ? 'unavailable' : s)), SEARCH_TIMEOUT_MS);
    // Keep watching even after the timeout, so a late fix still lands.
    const watchId = navigator.geolocation.watchPosition(
      ({ coords }) => {
        setPosition({
          latitude: coords.latitude,
          longitude: coords.longitude,
          altitude: coords.altitude ?? 0,
          accuracy: coords.accuracy,
        });
        setStatus('found');
      },
      () => setStatus((s) => (s === 'found' ? s : 'unavailable')),
      { enableHighAccuracy: true, maximumAge: 10000 },
    );
    return () => {
      clearTimeout(timeout);
      navigator.geolocation.clearWatch(watchId);
    };
  }, []);

  return { status, position };
}
