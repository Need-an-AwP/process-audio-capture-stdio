import { useEffect, useRef, useState } from 'react'
import { Select, SelectTrigger, SelectValue, SelectContent, SelectItem } from '@/components/ui/select'
import { Button } from '@/components/ui/button'
import { Switch } from '@/components/ui/switch'
import { Label } from '@/components/ui/label'
import { ThemeProvider } from '@/components/theme-provider'
import './App.css'
import UserAudioSpectrum from '@/components/UserAudioSpectrum'

function App() {
    const [audioSessions, setAudioSessions] = useState<any[]>([]);
    const [isCapturing, setIsCapturing] = useState<string>('');
    const audioContextRef = useRef<AudioContext | null>(null);
    const pcmPlayerNodeRef = useRef<AudioWorkletNode | null>(null);
    const audioRef = useRef<HTMLAudioElement | null>(null);
    const [analyser, setAnalyser] = useState<AnalyserNode | null>(null);
    const [displaySpectrum, setDisplaySpectrum] = useState<boolean>(true);

    const initAudio = async (format: any) => {
        if (!audioContextRef.current) {
            // const context = new AudioContext({ sampleRate: format.sampleRate });
            const context = new AudioContext();
            audioContextRef.current = context;
            try {
                await context.audioWorklet.addModule('audio-processor.js');
                const playerNode = new AudioWorkletNode(context, 'pcm-player', {
                    outputChannelCount: [format.channels],
                    processorOptions: {
                        sampleRate: format.sampleRate,
                        channels: format.channels
                    }
                });
                const destination = context.createMediaStreamDestination()
                const analyserNode = context.createAnalyser()
                setAnalyser(analyserNode);
                playerNode.connect(analyserNode)
                analyserNode.connect(destination)

                const stream = destination.stream
                if (audioRef.current) {
                    audioRef.current.srcObject = stream
                    audioRef.current.play()
                }

                pcmPlayerNodeRef.current = playerNode;

                console.log('Audio context and worklet initialized.');
            } catch (error) {
                console.error('Error loading audio worklet:', error);
            }
        }
    };

    const handlePcmData = (data: any) => {
        if (pcmPlayerNodeRef.current) {
            // console.log(`[App.tsx] Received pcm-data, size: ${data.byteLength}`);

            // using default array buffer
            const pcm16 = new Int16Array(data.buffer, data.byteOffset, data.byteLength / 2);
            const pcm32 = new Float32Array(pcm16.length);
            for (let i = 0; i < pcm32.length; i++) {
                pcm32[i] = pcm16[i] / 32768.0; // Convert from 16-bit int to float
            }
            /* 
            // Use DataView to ensure little-endian interpretation.
            const dataView = new DataView(data.buffer, data.byteOffset, data.byteLength);
            const pcm32 = new Float32Array(data.byteLength / 2);
            for (let i = 0; i < pcm32.length; i++) {
                // Read 16-bit signed integer at byte offset i * 2, with little-endian flag set to true.
                const pcm16Sample = dataView.getInt16(i * 2, true); 
                pcm32[i] = pcm16Sample / 32768.0; // Convert from 16-bit int to float
            }
            */

            pcmPlayerNodeRef.current.port.postMessage({ type: 'pcm-data', pcm: pcm32 });
        }
    };

    const handleCaptureFormat = (data: any) => {
        console.log('Received capture format:', data);
        initAudio(data.format);
    };

    const handleStartCapture = (pid: string) => {
        // window.ipcBridge.send('stop-capture')
        window.ipcBridge.send('start-capture', pid)
        setIsCapturing(pid)
    }

    const handleStopCapture = () => {
        window.ipcBridge.send('stop-capture')
        setIsCapturing('')
        setAnalyser(null);
        if (audioContextRef.current && audioContextRef.current.state !== 'closed') {
            audioContextRef.current.close().then(() => {
                console.log('AudioContext closed.');
                audioContextRef.current = null;
                pcmPlayerNodeRef.current = null;
            });
        }
        if (audioRef.current) {
            audioRef.current.pause()
            audioRef.current.srcObject = null
        }
    }

    useEffect(() => {
        window.ipcBridge.getAudioSessions()

        window.ipcBridge.receive('audio-sessions', (data) => {
            console.log(data)
            setAudioSessions(data)
        })

        window.ipcBridge.receive('capture-format', (data) => {
            handleCaptureFormat(data)
        })

        window.ipcBridge.receive('pcm-data', (data) => {
            handlePcmData(data)
        })

        return () => { }
    }, [])

    return (
        <ThemeProvider defaultTheme="dark" storageKey="vite-ui-theme">
            <div className='w-screen h-screen p-4'>
                <div className='w-full h-full flex flex-col justify-center items-center gap-4'>
                    <audio ref={audioRef} muted={true} controls={true} autoPlay={true} />

                    <div className='flex flex-row gap-2 items-center'>
                        <Select
                            value={isCapturing}
                            onValueChange={handleStartCapture}
                        >
                            <SelectTrigger>
                                <SelectValue placeholder='Select a process' />
                            </SelectTrigger>
                            <SelectContent>
                                {audioSessions.map((session) => (
                                    // pid is number, select value is string
                                    <SelectItem key={session.pid} value={session.pid.toString()}>
                                        {session.processName}-{session.pid}
                                    </SelectItem>
                                ))}
                            </SelectContent>
                        </Select>
                        <Switch checked={displaySpectrum} onCheckedChange={setDisplaySpectrum} />
                        <Label className='text-xs text-muted-foreground'>Display Spectrum</Label>
                    </div>

                    {isCapturing.length > 0 && analyser && displaySpectrum && <>
                        <Button onClick={handleStopCapture}>Stop Capture</Button>
                        <UserAudioSpectrum
                            displayStyle='bar'
                            className='w-full aspect-video'
                            analyser={analyser}
                        />
                    </>}

                    {/* {analyserRef.current &&  */}
                </div>
            </div>
        </ThemeProvider>

    )
}

export default App
