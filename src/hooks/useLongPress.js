import { useRef } from 'react';

// Pointer handlers that call `callback` after a press of `ms`. Returns no
// handlers when `callback` is missing (used for the mock-only controls).
export function useLongPress(callback, ms = 800) {
  const timer = useRef(null);
  if (!callback) return {};
  const cancel = () => clearTimeout(timer.current);
  return {
    onPointerDown: () => {
      cancel();
      timer.current = setTimeout(callback, ms);
    },
    onPointerUp: cancel,
    onPointerLeave: cancel,
    onPointerCancel: cancel,
    onContextMenu: (e) => e.preventDefault(),
  };
}
