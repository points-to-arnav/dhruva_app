import tailwindcss from '@tailwindcss/vite';
import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';

// The phone opens the app as http://localhost:5173 through `adb reverse`,
// because WebUSB only works on secure origins (https or localhost). Bound to
// 127.0.0.1 because adb forwards over IPv4, and Vite's "localhost" can mean
// IPv6 ::1 only (common on Windows).
export default defineConfig({
  plugins: [react(), tailwindcss()],
  server: { host: '127.0.0.1', port: 5173, strictPort: true },
  preview: { host: '127.0.0.1', port: 5173, strictPort: true },
});
