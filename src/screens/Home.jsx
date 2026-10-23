import { DongleStatus } from '../components/DongleStatus';
import { NeedChips } from '../components/NeedChips';
import { PeopleStepper } from '../components/PeopleStepper';
import { Screen } from '../components/Screen';
import { SosButton } from '../components/SosButton';
import { useSos } from '../state/useSos';

const LOCATION_TEXT = { demo: 'Demo location', found: 'Location found', searching: 'Finding location…', unavailable: null };

export function Home({ onSos }) {
  const { connected } = useSos();
  return <Screen>{connected ? <Ready onSos={onSos} /> : <NoDongle />}</Screen>;
}

function Ready({ onSos }) {
  const { nodesInRange, location, needs, people, toggleNeed, setPeople } = useSos();

  const nodes =
    nodesInRange === null
      ? null
      : nodesInRange === 0
        ? 'No nodes in range'
        : nodesInRange === 1
          ? '1 node in range'
          : `${nodesInRange} nodes in range`;
  const meta = [nodes, LOCATION_TEXT[location]].filter(Boolean).join(' · ');
  const noLocation = location === 'unavailable';

  return (
    <>
      <DongleStatus />
      <SosButton onClick={onSos} />
      <p className="mt-3.5 text-center text-[17px] leading-6">Press once. It is saved and sent automatically.</p>
      {meta ? <p className="mt-1 text-center text-[15px] text-muted">{meta}</p> : null}
      {noLocation ? <LocationWarning /> : null}
      <div className={`h-px bg-divider ${noLocation ? 'mt-3.5 mb-3' : 'mt-[18px] mb-3.5'}`} />
      <NeedChips selected={needs} onToggle={toggleNeed} />
      <div className="mt-3">
        <PeopleStepper value={people} onChange={setPeople} />
      </div>
    </>
  );
}

function LocationWarning() {
  return (
    <div role="alert" className="mt-2.5 flex items-start gap-3 rounded-[14px] border-[1.5px] border-ink px-3.5 py-3">
      <span className="flex size-7 flex-none items-center justify-center rounded-full border-2 border-ink text-base font-extrabold">
        !
      </span>
      <div>
        <div className="text-[17px] leading-[22px] font-bold">Location not found</div>
        <div className="mt-0.5 text-[15px] leading-5 text-soft">
          Rescuers will not know where you are. Try near a window or outside.
        </div>
      </div>
    </div>
  );
}

const UNSUPPORTED_TEXT = {
  insecure: 'Chrome only allows USB on secure pages. Open Dhruva from http://localhost or an https:// address.',
  'no-api': 'This browser can’t talk to USB devices. Open this page in Chrome on an Android phone.',
};

function NoDongle() {
  const { supported, unsupportedReason, connect, connectError } = useSos();
  const steps = [
    'Connect the dongle to your phone with the USB-C cable.',
    'Tap Connect dongle, pick the dongle, then tap Allow.',
    'This screen updates by itself.',
  ];

  return (
    <>
      <DongleStatus />
      <SosButton disabled />
      <h1 className="mt-7 text-center text-[26px] leading-8 font-bold">Plug in the dongle to send SOS</h1>

      {supported ? (
        <>
          <ol className="mt-6 flex flex-col gap-4">
            {steps.map((step, i) => (
              <li key={step} className="flex items-start gap-3.5">
                <span className="flex size-8 flex-none items-center justify-center rounded-full border-2 border-ink text-base font-bold">
                  {i + 1}
                </span>
                <span className="pt-1 text-[17px] leading-6">{step}</span>
              </li>
            ))}
          </ol>
          <button
            type="button"
            onClick={connect}
            className="mt-7 h-14 cursor-pointer rounded-full bg-ink text-[19px] font-bold text-bg active:opacity-80"
          >
            Connect dongle
          </button>
          {connectError ? (
            <p role="alert" className="mt-3 text-center text-[15px] text-soft">
              Could not open the dongle: {connectError}
            </p>
          ) : null}
        </>
      ) : (
        <p role="alert" className="mt-6 rounded-[14px] border-[1.5px] border-ink px-3.5 py-3 text-[17px] leading-6">
          {UNSUPPORTED_TEXT[unsupportedReason]}
        </p>
      )}

      <p className="mt-auto pt-6 text-center text-[15px] text-muted">Still nothing? Unplug the dongle and plug it in again.</p>
    </>
  );
}
