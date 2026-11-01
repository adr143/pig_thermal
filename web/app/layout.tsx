import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "Pig Monitoring System",
  description: "Live webcam + thermal monitoring with pig detection and classification",
};

export default function RootLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  return (
    <html lang="en">
      <body className="min-h-screen bg-bg text-gray-200">{children}</body>
    </html>
  );
}
