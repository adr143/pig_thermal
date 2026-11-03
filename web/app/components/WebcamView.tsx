"use client";

interface Props {
  img: string | null;
  detections: {
    bbox: [number, number, number, number];
    conf: number;
    cls: string | null;
    cls_conf: number | null;
  }[] | null;
}

export default function WebcamView({ img, detections }: Props) {
  return (
    <div className="flex flex-col gap-2">
      <div className="relative w-full bg-black rounded-md overflow-hidden border border-slate-800">
        {img ? (

          <img
            src={img}
            alt="webcam stream"
            className="w-full h-auto block"
            style={{ aspectRatio: "16 / 9" }}
          />
        ) : (
          <div className="w-full aspect-video flex items-center justify-center text-gray-500">
            Waiting for webcam stream...
          </div>
        )}
      </div>
      {detections && detections.length > 0 && (
        <div className="text-xs text-gray-400">
          {detections.length} pig{detections.length !== 1 && "s"} detected
        </div>
      )}
    </div>
  );
}
