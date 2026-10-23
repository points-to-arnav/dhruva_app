import * as Location from 'expo-location';
import { useEffect, useState } from 'react';

export type LocationStatus = 'searching' | 'found' | 'unavailable';

// Give up on "searching" after this long and warn the user instead.
const SEARCH_TIMEOUT_MS = 20000;

export function useLocationStatus(): { status: LocationStatus; coords: Location.LocationObjectCoords | null } {
  const [status, setStatus] = useState<LocationStatus>('searching');
  const [coords, setCoords] = useState<Location.LocationObjectCoords | null>(null);

  useEffect(() => {
    let cancelled = false;
    let subscription: Location.LocationSubscription | null = null;
    const timeout = setTimeout(() => {
      if (!cancelled) setStatus((s) => (s === 'searching' ? 'unavailable' : s));
    }, SEARCH_TIMEOUT_MS);

    (async () => {
      try {
        const { granted } = await Location.requestForegroundPermissionsAsync();
        if (cancelled) return;
        if (!granted) {
          setStatus('unavailable');
          return;
        }
        // Keep watching even after the timeout, so a late fix still lands.
        subscription = await Location.watchPositionAsync(
          { accuracy: Location.Accuracy.Balanced, timeInterval: 10000, distanceInterval: 10 },
          (position) => {
            setCoords(position.coords);
            setStatus('found');
          },
          () => setStatus((s) => (s === 'found' ? s : 'unavailable')),
        );
        if (cancelled) subscription.remove();
      } catch {
        if (!cancelled) setStatus('unavailable');
      }
    })();

    return () => {
      cancelled = true;
      clearTimeout(timeout);
      subscription?.remove();
    };
  }, []);

  return { status, coords };
}
