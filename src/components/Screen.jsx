// One phone-width column. Content fills the screen, so `mt-auto` pins
// footers to the bottom; it scrolls only when it doesn't fit.
export function Screen({ children }) {
  return <main className="mx-auto flex min-h-dvh w-full max-w-[430px] flex-col px-5 pt-5 pb-6">{children}</main>;
}
