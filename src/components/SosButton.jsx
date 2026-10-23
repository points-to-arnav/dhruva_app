export function SosButton({ onClick, disabled }) {
  return (
    <div
      className={`mx-auto mt-3 flex size-[246px] items-center justify-center rounded-full border-2 ${
        disabled ? 'border-track' : 'border-sos-ring'
      }`}
    >
      <button
        type="button"
        onClick={onClick}
        disabled={disabled}
        aria-label={disabled ? 'Send SOS, unavailable until the dongle is connected' : 'Send SOS'}
        className={`size-[220px] rounded-full text-[65px] font-extrabold tracking-[0.04em] transition-transform ${
          disabled ? 'bg-off text-off-ink' : 'cursor-pointer bg-sos text-white active:scale-[0.97]'
        }`}
      >
        SOS
      </button>
    </div>
  );
}
