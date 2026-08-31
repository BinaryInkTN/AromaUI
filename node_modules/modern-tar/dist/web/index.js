import { a as normalizeBody, i as isBodyless, n as createUnpacker, r as transformHeader, t as createTarPacker$1 } from "../packer-kJPaRbFA.js";
//#region src/web/pack.ts
function createTarPacker() {
	let streamController;
	let packer;
	let bodyController = null;
	let resume = null;
	let drain = null;
	let stopped = false;
	let stopReason;
	const unblock = () => {
		resume?.();
		resume = null;
		drain = null;
	};
	const stop = (reason) => {
		if (stopped) return;
		stopped = true;
		stopReason = reason;
		bodyController?.error(reason);
		bodyController = null;
		streamController.error(reason);
		unblock();
	};
	const abortBody = (reason = AbortSignal.abort().reason) => {
		bodyController = null;
		stop(reason);
	};
	const checkStopped = () => {
		if (stopped) throw stopReason;
	};
	const emit = (operation) => {
		checkStopped();
		try {
			operation();
		} catch (error) {
			stop(error);
			throw error;
		}
		const pending = drain;
		if (!pending) return;
		const signal = bodyController?.signal;
		if (!signal) return;
		if (signal.aborted) abortBody(signal.reason);
		else signal.onabort = () => abortBody(signal.reason);
		return pending.then(checkStopped);
	};
	return {
		readable: new ReadableStream({
			start(controller) {
				streamController = controller;
				packer = createTarPacker$1((chunk) => {
					const buffer = chunk.buffer;
					controller.enqueue(buffer instanceof ArrayBuffer && !Reflect.get(buffer, "resizable") ? chunk : new Uint8Array(chunk));
					if ((controller.desiredSize ?? 1) <= 0) drain ||= new Promise((resolve) => {
						resume = resolve;
					});
				});
			},
			pull: unblock,
			cancel(reason = AbortSignal.abort().reason) {
				stop(reason);
			}
		}, new ByteLengthQueuingStrategy({ highWaterMark: 8388608 })),
		controller: {
			add(header) {
				try {
					checkStopped();
					if (bodyController) throw new Error("Previous entry must be completed before adding a new one");
					packer.add(header);
				} catch (error) {
					stop(error);
					throw error;
				}
				return new WritableStream({
					start(controller) {
						bodyController = controller;
					},
					write(chunk) {
						return emit(() => packer.write(chunk));
					},
					close() {
						const result = emit(() => packer.endEntry());
						if (!result) {
							bodyController = null;
							return;
						}
						return result.then(() => {
							bodyController = null;
						});
					},
					abort: abortBody
				});
			},
			finalize() {
				try {
					checkStopped();
					if (bodyController) throw new Error("Cannot finalize while an entry is still active");
					packer.finalize();
					streamController.close();
				} catch (error) {
					stop(error);
					throw error;
				}
			},
			error(err) {
				stop(err);
			}
		}
	};
}
//#endregion
//#region src/web/stream-utils.ts
async function streamToBuffer(stream) {
	const chunks = [];
	const reader = stream.getReader();
	let totalLength = 0;
	try {
		while (true) {
			const { done, value } = await reader.read();
			if (done) break;
			chunks.push(value);
			totalLength += value.length;
		}
		const result = new Uint8Array(totalLength);
		let offset = 0;
		for (const chunk of chunks) {
			result.set(chunk, offset);
			offset += chunk.length;
		}
		return result;
	} finally {
		reader.releaseLock();
	}
}
const drain = (stream) => stream.pipeTo(new WritableStream());
//#endregion
//#region src/web/unpack.ts
const BUFFER_LIMIT = 1048576;
const RESUME_LIMIT = BUFFER_LIMIT / 2;
function createTarDecoder(options = {}) {
	const unpacker = createUnpacker(options);
	const strict = options.strict ?? false;
	let controller = null;
	let bodyController = null;
	let pumping = false;
	let blocked = false;
	let resume = null;
	let abortHooked = false;
	let eofReached = false;
	let sourceEnded = false;
	let closed = false;
	const unblock = () => {
		resume?.();
		resume = null;
	};
	const closeBody = () => {
		try {
			bodyController?.close();
		} catch {}
		bodyController = null;
	};
	const fail = (reason) => {
		if (closed) return;
		closed = true;
		try {
			bodyController?.error(reason);
		} catch {}
		bodyController = null;
		try {
			controller.error(reason);
		} catch {}
		controller = null;
		unblock();
	};
	const finish = () => {
		if (closed) return;
		closed = true;
		closeBody();
		try {
			controller.close();
		} catch {}
		controller = null;
		unblock();
	};
	const truncateOrFinish = () => {
		if (strict) throw new Error("Tar archive is truncated.");
		finish();
	};
	const pump = () => {
		if (pumping || closed || !controller) return;
		blocked = false;
		pumping = true;
		try {
			while (true) {
				if (eofReached) {
					if (sourceEnded) {
						unpacker.validateEOF();
						finish();
					}
					break;
				}
				if (unpacker.isEntryActive()) {
					if (sourceEnded && !unpacker.canFinish()) {
						truncateOrFinish();
						break;
					}
					if (bodyController) {
						if ((bodyController.desiredSize ?? 1) <= 0) {
							blocked = true;
							break;
						}
						if (unpacker.streamBody((c) => (bodyController.enqueue(c), (bodyController.desiredSize ?? 1) > 0)) === 0 && !unpacker.isBodyComplete()) {
							if (sourceEnded) truncateOrFinish();
							break;
						}
					} else if (!unpacker.skipEntry()) {
						if (sourceEnded) truncateOrFinish();
						break;
					}
					if (unpacker.isBodyComplete()) {
						closeBody();
						if (!unpacker.skipPadding()) {
							if (sourceEnded) truncateOrFinish();
							break;
						}
					}
				} else {
					if ((controller.desiredSize ?? 0) < 0) {
						blocked = true;
						break;
					}
					const header = unpacker.readHeader();
					if (header === null) {
						if (sourceEnded) finish();
						break;
					}
					if (header === void 0) {
						if (sourceEnded) {
							unpacker.validateEOF();
							finish();
							break;
						}
						eofReached = true;
						break;
					}
					controller.enqueue({
						header,
						body: new ReadableStream({
							start(c) {
								if (header.size === 0) c.close();
								else bodyController = c;
							},
							pull: pump,
							cancel() {
								bodyController = null;
								pump();
							}
						})
					});
				}
			}
		} catch (error) {
			fail(error);
			throw error;
		} finally {
			pumping = false;
		}
		if (resume && (!blocked || unpacker.available() < RESUME_LIMIT)) unblock();
	};
	return {
		readable: new ReadableStream({
			start(c) {
				controller = c;
			},
			pull: pump,
			cancel(reason) {
				unpacker.end();
				if (reason !== void 0) fail(reason);
				else finish();
			}
		}, { highWaterMark: 2 }),
		writable: new WritableStream({
			write(chunk, controller) {
				try {
					if (eofReached && strict && chunk.some((byte) => byte !== 0)) throw new Error("Invalid EOF.");
					unpacker.write(chunk);
					pump();
					if (blocked && unpacker.available() >= BUFFER_LIMIT) {
						if (!abortHooked) {
							controller.signal.onabort = unblock;
							abortHooked = true;
						}
						return new Promise((resolve) => resume = resolve);
					}
				} catch (error) {
					fail(error);
					throw error;
				}
			},
			close() {
				try {
					sourceEnded = true;
					unpacker.end();
					pump();
				} catch (error) {
					fail(error);
					throw error;
				}
			},
			abort(reason) {
				fail(reason);
			}
		})
	};
}
//#endregion
//#region src/web/helpers.ts
async function packTar(entries) {
	const { readable, controller } = createTarPacker();
	const archive = streamToBuffer(readable);
	try {
		for (const entry of entries) {
			const entryStream = controller.add(entry.header);
			const body = "body" in entry ? entry.body : entry.data;
			if (!body) {
				await entryStream.close();
				continue;
			}
			if (body instanceof ReadableStream) await body.pipeTo(entryStream);
			else if (body instanceof Blob) await body.stream().pipeTo(entryStream);
			else try {
				const chunk = await normalizeBody(body);
				if (chunk.length > 0) {
					const writer = entryStream.getWriter();
					await writer.write(chunk);
					await writer.close();
				} else await entryStream.close();
			} catch {
				throw new TypeError(`Unsupported content type for entry "${entry.header.name}".`);
			}
		}
		controller.finalize();
	} catch (error) {
		controller.error(error);
	}
	return archive;
}
async function unpackTar(archive, options = {}) {
	if (!(archive instanceof ReadableStream)) return unpackTarBuffer(archive instanceof Uint8Array ? archive : new Uint8Array(archive), options);
	const results = [];
	const reader = archive.pipeThrough(createTarDecoder(options)).getReader();
	try {
		while (true) {
			const { done, value: entry } = await reader.read();
			if (done) break;
			let processedHeader;
			try {
				processedHeader = transformHeader(entry.header, options);
			} catch (error) {
				await entry.body.cancel();
				throw error;
			}
			if (processedHeader === null) {
				await drain(entry.body);
				continue;
			}
			if (isBodyless(processedHeader)) {
				await drain(entry.body);
				results.push({ header: processedHeader });
			} else results.push({
				header: processedHeader,
				data: await streamToBuffer(entry.body)
			});
		}
	} catch (error) {
		await reader.cancel(error).catch(() => {});
		throw error;
	} finally {
		reader.releaseLock();
	}
	return results;
}
function unpackTarBuffer(archive, options) {
	const unpacker = createUnpacker(options);
	const strict = options.strict ?? false;
	const results = [];
	unpacker.write(archive);
	unpacker.end();
	while (true) {
		const header = unpacker.readHeader();
		if (header === void 0) break;
		if (header === null) {
			if (strict) throw new Error("Tar archive is truncated.");
			break;
		}
		const processedHeader = transformHeader(header, options);
		if (processedHeader === null) {
			const skipped = unpacker.skipEntry();
			if (!skipped && strict) throw new Error("Tar archive is truncated.");
			if (!skipped) break;
			continue;
		}
		if (isBodyless(processedHeader)) {
			const skipped = unpacker.skipEntry();
			if (!skipped && strict) throw new Error("Tar archive is truncated.");
			results.push({ header: processedHeader });
			if (!skipped) break;
			continue;
		}
		let size = header.size;
		if (size < 0 || !unpacker.canFinish()) {
			if (strict) throw new Error("Tar archive is truncated.");
			size = unpacker.bodyBytes();
		}
		const data = new Uint8Array(size);
		let offset = 0;
		unpacker.streamBody((chunk) => {
			data.set(chunk, offset);
			offset += chunk.length;
			return true;
		});
		const bodyComplete = unpacker.isBodyComplete();
		let paddingComplete = true;
		if (bodyComplete) {
			paddingComplete = unpacker.skipPadding();
			if (!paddingComplete && strict) throw new Error("Tar archive is truncated.");
		}
		results.push({
			header: processedHeader,
			data
		});
		if (!bodyComplete || !paddingComplete) break;
	}
	unpacker.validateEOF();
	return results;
}
//#endregion
export { createTarDecoder, createTarPacker, packTar, unpackTar };
