import { CheckIcon } from '../components/CheckIcon';
import { DongleStatus } from '../components/DongleStatus';
import { RequestCard } from '../components/RequestCard';
import { Screen } from '../components/Screen';
import { Timeline } from '../components/Timeline';
import { useLongPress } from '../hooks/useLongPress';
import { describeRequest, formatTime } from '../state/sos';
import { useSos } from '../state/useSos';

export function Status({ onStartOver }) {
  const { sentRequest, message, sendError, isMock, sendSos, reset } = useSos();
  const { state } = message;
  const saved = state !== null && state !== 'QUEUED';
  const delivered = state === 'DELIVERED';
  const failed = state === 'FAILED';
  const waiting = state === 'WAITING';
  const reached = delivered || state === 'SENT_TO_MESH' || message.nextHop !== null;

  // Mock only: long-press the banner to start over.
  const bannerPress = useLongPress(
    isMock
      ? () => {
          reset();
          onStartOver();
        }
      : null,
    1000,
  );

  const steps = [
    {
      title: 'Saved on your dongle',
      detail: saved && message.id ? message.id : sendError ? 'Not saved' : 'Saving…',
      state: saved ? 'done' : 'active',
    },
    reached
      ? {
          title: 'Reached the network',
          detail: message.nextHop ? `Via ${message.nextHop}` : 'Handed to a nearby node',
          state: 'done',
        }
      : {
          title: 'Reaching the network',
          detail: waiting
            ? 'No node in range yet. Will keep trying.'
            : state === 'TRANSMITTING'
              ? 'Sending to a nearby node'
              : 'Looking for a nearby node',
          state: saved ? 'active' : 'pending',
        },
    {
      title: delivered ? 'Rescue team received it' : 'Rescue team receives it',
      detail: message.deliveredAt ? `Confirmed at ${formatTime(message.deliveredAt)}` : failed ? 'Did not arrive' : 'Not yet',
      state: delivered ? 'done' : 'pending',
    },
  ];

  const [heading, body] = delivered
    ? ['Rescue team has your request', 'Stay where you are if it is safe, and keep the dongle on.']
    : failed
      ? ['Your SOS did not reach the rescue team', 'It ran out of hops on the way. Send it again to try another route.']
      : waiting
        ? [
            'Saved, waiting for the network',
            'No rescue node is in range yet. The dongle keeps your message and sends it as soon as one is. Move outside or higher if you can.',
          ]
        : [
            saved ? 'Saved on your dongle' : 'Saving to your dongle',
            'You do not need to press anything again. The dongle keeps trying until the network has it.',
          ];

  return (
    <Screen>
      <DongleStatus />
      <div className="select-none" {...bannerPress}>
        {delivered ? (
          <div className="mt-4 flex h-[52px] items-center justify-center gap-2.5 rounded-xl border-2 border-ink text-[17px] font-extrabold tracking-[0.08em]">
            <CheckIcon size={20} className="stroke-ink" />
            SOS DELIVERED
          </div>
        ) : (
          <div className="mt-4 flex h-[52px] items-center justify-center rounded-xl bg-sos text-[17px] font-extrabold tracking-[0.08em] text-white">
            {failed ? 'SOS NOT DELIVERED' : 'SOS REQUEST ACTIVE'}
          </div>
        )}
      </div>

      <h1 className="mt-7 text-[30px] leading-9 font-bold">{heading}</h1>
      <p className="mt-2.5 text-[17px] leading-6 text-soft">{body}</p>

      {failed ? (
        <button
          type="button"
          onClick={sendSos}
          className="mt-5 h-12 cursor-pointer rounded-full bg-ink text-[17px] font-bold text-bg"
        >
          Send again
        </button>
      ) : null}

      {sendError && !saved ? (
        <div role="alert" className="mt-5 flex flex-col gap-1 rounded-[14px] border-[1.5px] border-ink p-3.5">
          <div className="text-[17px] font-bold">Could not reach the dongle</div>
          <div className="text-[15px] leading-5 text-soft">{sendError}. Check the cable, then try again.</div>
          <button
            type="button"
            onClick={sendSos}
            className="mt-2.5 h-12 cursor-pointer rounded-full bg-ink text-[17px] font-bold text-bg"
          >
            Try again
          </button>
        </div>
      ) : null}

      <div className="mt-8">
        <Timeline steps={steps} />
      </div>

      {sentRequest ? (
        <div className="mt-auto pt-6">
          <RequestCard summary={describeRequest(sentRequest)} />
        </div>
      ) : null}
    </Screen>
  );
}
