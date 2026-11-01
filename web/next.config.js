/** @type {import('next').NextConfig} */
const nextConfig = {
  reactStrictMode: true,
  // Proxy HTTP API calls to Flask. WebSocket connections are made directly
  // to Flask because Next.js rewrites do not proxy WebSocket upgrades.
  async rewrites() {
    const backend = process.env.BACKEND_URL || "http://127.0.0.1:5000";
    return [
      { source: "/api/:path*", destination: `${backend}/api/:path*` },
    ];
  },
};

module.exports = nextConfig;
