import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { fileURLToPath, URL } from 'node:url';

const BLAZE_TARGET = process.env.BLAZEKV_URL ?? 'http://localhost:9121';

export default defineConfig({
  plugins: [react()],
  resolve: {
    alias: {
      '@': fileURLToPath(new URL('./src', import.meta.url)),
    },
  },
  build: {
    // Recharts is the dominant dependency; a single ~535 kB vendor chunk is
    // expected for this SPA and does not warrant lazy-loading.
    chunkSizeWarningLimit: 700,
    rollupOptions: {
      output: {
        manualChunks: {
          charts: ['recharts'],
          react: ['react', 'react-dom'],
        },
      },
    },
  },
  server: {
    port: 5173,
    proxy: {
      '/api': {
        target: BLAZE_TARGET,
        changeOrigin: true,
      },
      '/metrics': {
        target: BLAZE_TARGET,
        changeOrigin: true,
      },
    },
  },
});
