import { useLongPress } from '../hooks/useLongPress';
import { useSos } from '../state/useSos';

export function DongleStatus() {
  const { connected, battery, isMock, toggleMockConnection, toggleMockNodes } = useSos();
  // Mock only: long-press to unplug the dongle, or to drop the mesh nodes.
  const connectionPress = useLongPress(isMock ? toggleMockConnection : null);
  const nodesPress = useLongPress(isMock ? toggleMockNodes : null);

  return (
    <div className="flex h-8 items-center justify-between text-[15px] select-none">
      <div className="flex items-center gap-2 font-semibold" {...connectionPress}>
        <span className={`size-2.5 rounded-full ${connected ? 'bg-ink' : 'border-2 border-ink'}`} />
        <span>{connected ? 'Dongle connected' : 'Dongle not connected'}</span>
      </div>
      {connected && battery !== null ? (
        <div className="text-muted" {...nodesPress}>
          Battery {battery}%
        </div>
      ) : null}
    </div>
  );
}
