/** @type {import('tailwindcss').Config} */
export default {
  content: ['./index.html', './src/**/*.{ts,tsx}'],
  theme: {
    extend: {
      colors: {
        canvas: '#0B0E14',
        surface: '#0F1117',
        elevated: '#141824',
        blaze: {
          DEFAULT: '#F5A623',
          bright: '#FF8A3D',
          dim: '#8A5A17',
        },
        good: '#4ADE80',
        bad: '#F87171',
      },
      fontFamily: {
        sans: [
          'Inter',
          'ui-sans-serif',
          'system-ui',
          '-apple-system',
          'Segoe UI',
          'Roboto',
          'sans-serif',
        ],
        mono: [
          'JetBrains Mono',
          'ui-monospace',
          'SFMono-Regular',
          'Menlo',
          'Consolas',
          'monospace',
        ],
      },
      boxShadow: {
        card: '0 1px 2px rgba(0,0,0,0.4), 0 0 0 1px rgba(255,255,255,0.02)',
      },
      keyframes: {
        fadein: {
          '0%': { opacity: '0', transform: 'translateY(2px)' },
          '100%': { opacity: '1', transform: 'translateY(0)' },
        },
        pulsedot: {
          '0%, 100%': { opacity: '1' },
          '50%': { opacity: '0.35' },
        },
      },
      animation: {
        fadein: 'fadein 0.25s ease-out',
        pulsedot: 'pulsedot 1.8s ease-in-out infinite',
      },
    },
  },
  plugins: [],
};
