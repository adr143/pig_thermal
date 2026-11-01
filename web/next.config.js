/** @type {import('next').NextConfig} */
const nextConfig = {
  reactStrictMode: true,
  // Proxy /api/* and /ws/* to the Flask backend on port 5000 to avoid CORS
  // headaches during development. In production you can either keep this proxy
  // or set CORS_ORIGINS on the backend to include the Next.js host.
  async rewrites() {
    const backend = process.env.BACKEND_URL || "http://127.0.0.1:5000";
    return [
      { source: "/api/:path*", destination: `${backend}/api/:path*` },
      { source: "/ws/:path*", destination: `${backend}/ws/:path*` },
    ];
  },
};

module.exports = nextConfig;
