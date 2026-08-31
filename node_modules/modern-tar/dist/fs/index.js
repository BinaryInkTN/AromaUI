import { a as normalizeBody, c as LINK, l as SYMLINK, n as createUnpacker, o as DIRECTORY, r as transformHeader, s as FILE, t as createTarPacker } from "../packer-kJPaRbFA.js";
import * as fs from "node:fs";
import * as fsp from "node:fs/promises";
import { cpus } from "node:os";
import * as path from "node:path";
import { Readable, Writable } from "node:stream";
//#region src/fs/path.ts
function validateBounds(targetPath, destDir, errorMessage) {
	const target = path.resolve(targetPath);
	const dest = path.resolve(destDir);
	if (target !== dest && !target.startsWith(dest + path.sep)) throw new Error(errorMessage);
}
const win32Reserved = {
	":": "",
	"<": "",
	">": "",
	"|": "",
	"?": "",
	"*": "",
	"\"": ""
};
const pathAlias = /\/\/|(?:^|\/)\.(?:\/|$)/;
function normalizeName(name) {
	const path = name.replace(/\\/g, "/");
	if (path.split("/").includes("..") || /^[a-zA-Z]:\.\./.test(path)) throw new Error(`${name} points outside extraction directory`);
	let relative = path;
	if (/^[a-zA-Z]:/.test(relative)) relative = relative.replace(/^[a-zA-Z]:[/\\]?/, "");
	else if (relative.startsWith("/")) relative = relative.replace(/^\/+/, "");
	if (process.platform === "win32") return relative.replace(/[<>:"|?*]/g, (char) => win32Reserved[char]);
	return relative;
}
const normalizeHeaderName = (s) => {
	const name = normalizeName(s.replace(/[\\/]+$/, ""));
	return pathAlias.test(name) ? path.posix.normalize(name) : name;
};
//#endregion
//#region src/fs/pack.ts
const BIGINT_STAT = { bigint: true };
const WITH_FILE_TYPES = { withFileTypes: true };
const packTarSources = packTar;
function packTar(sources, options = {}) {
	const results = /* @__PURE__ */ new Map();
	const fileHandles = /* @__PURE__ */ new Map();
	const bodyStreams = /* @__PURE__ */ new Set();
	let resume = null;
	let drain = null;
	let resumeWriter = null;
	let cancelError;
	const unblock = () => {
		const resolve = resume;
		resume = null;
		drain = null;
		resolve?.();
	};
	const wakeWriter = () => {
		resumeWriter?.();
		resumeWriter = null;
	};
	const destroyBody = (body, reason) => {
		bodyStreams.delete(body);
		body.destroy(reason);
	};
	const closeHandle = (handle) => {
		const closing = fileHandles.get(handle);
		if (closing !== null) return closing;
		const promise = handle.close().finally(() => fileHandles.delete(handle));
		fileHandles.set(handle, promise);
		return promise;
	};
	const stop = async (reason) => {
		for (const body of bodyStreams) destroyBody(body, reason);
		const closing = Promise.allSettled([...fileHandles.keys()].map(closeHandle));
		results.clear();
		wakeWriter();
		for (const result of await closing) if (result.status === "rejected") throw result.reason;
	};
	const stream = new Readable({
		highWaterMark: 8388608,
		read: unblock,
		destroy(error, callback) {
			cancelError = error ?? AbortSignal.abort().reason;
			unblock();
			stop(cancelError).then(() => callback(error), (closeError) => callback(error ?? closeError));
		}
	});
	const onError = (error) => stream.destroy(error);
	const packer = createTarPacker((chunk) => {
		if (stream.destroyed) throw cancelError;
		if (!stream.push(Buffer.from(chunk)) && !drain) drain = new Promise((resolve) => {
			resume = resolve;
		});
	});
	(async () => {
		const { dereference = false, filter, map, baseDir, concurrency = cpus().length || 8 } = options;
		let directoryPath;
		let realBaseDir;
		let jobs;
		if (typeof sources === "string") {
			const source = path.resolve(sources);
			directoryPath = source;
			const before = await fsp.stat(source, BIGINT_STAT);
			if (stream.destroyed) return;
			const entries = await fsp.readdir(source, WITH_FILE_TYPES);
			if (stream.destroyed) return;
			const after = await fsp.stat(source, BIGINT_STAT);
			if (stream.destroyed) return;
			jobs = before.dev === after.dev && before.ino === after.ino ? entries.map((entry) => ({
				type: entry.isDirectory() ? DIRECTORY : FILE,
				source: path.join(source, entry.name),
				target: entry.name
			})) : [];
		} else jobs = sources.map((source) => ({ ...source }));
		const seenHardlinks = /* @__PURE__ */ new Map();
		let jobIndex = 0;
		let writeIndex = 0;
		let activeWorkers = 0;
		let allJobsQueued = false;
		const writeStreamBody = async (body) => {
			try {
				for await (const chunk of body) {
					if (stream.destroyed) return;
					packer.write(chunk instanceof Uint8Array ? chunk : Buffer.from(chunk));
					if (drain) await drain;
				}
			} finally {
				body.off("error", onError);
				bodyStreams.delete(body);
			}
		};
		const writer = async () => {
			const readBufferSmall = Buffer.alloc(65536);
			let readBufferLarge = null;
			while (true) {
				if (stream.destroyed) return;
				if (allJobsQueued && writeIndex >= jobs.length) break;
				if (!results.has(writeIndex)) {
					await new Promise((resolve) => {
						resumeWriter = resolve;
					});
					continue;
				}
				const result = results.get(writeIndex);
				results.delete(writeIndex);
				if (!result) {
					writeIndex++;
					controller();
					continue;
				}
				if (result.hardlinkId) {
					const originalName = result.header.name;
					const hardlinkTarget = seenHardlinks.get(result.hardlinkId);
					if (hardlinkTarget) {
						if (result.body && !(result.body instanceof Uint8Array) && !(result.body instanceof Readable)) await closeHandle(result.body.handle);
						result.body = void 0;
						result.header.type = LINK;
						result.header.linkname = hardlinkTarget.originalName;
						result.header.size = 0;
					}
					if (map) result.header = map(result.header);
					if (hardlinkTarget) {
						if (result.header.linkname === hardlinkTarget.originalName) result.header.linkname = hardlinkTarget.mappedName;
					} else seenHardlinks.set(result.hardlinkId, {
						originalName,
						mappedName: result.header.name
					});
				}
				packer.add(result.header);
				if (drain) await drain;
				if (stream.destroyed) return;
				if (result.body) if (result.body instanceof Uint8Array) {
					if (result.body.length > 0) {
						packer.write(result.body);
						if (drain) await drain;
					}
				} else if (result.body instanceof Readable) await writeStreamBody(result.body);
				else {
					const { handle, size } = result.body;
					const readBuffer = size > 1048576 ? readBufferLarge ??= Buffer.alloc(1048576) : readBufferSmall;
					try {
						let bytesLeft = size;
						while (bytesLeft > 0 && !stream.destroyed) {
							const { bytesRead } = await handle.read(readBuffer, 0, Math.min(bytesLeft, readBuffer.length), null);
							if (bytesRead === 0) break;
							packer.write(readBuffer.subarray(0, bytesRead));
							bytesLeft -= bytesRead;
							if (drain) await drain;
						}
					} finally {
						await closeHandle(handle);
					}
				}
				if (stream.destroyed) return;
				packer.endEntry();
				if (drain) await drain;
				writeIndex++;
				controller();
			}
		};
		const controller = () => {
			if (stream.destroyed || allJobsQueued) return;
			while (activeWorkers < concurrency && jobIndex < jobs.length && jobIndex - writeIndex < concurrency) {
				activeWorkers++;
				const currentIndex = jobIndex++;
				processJob(jobs[currentIndex], currentIndex).catch(onError).finally(() => {
					activeWorkers--;
					controller();
				});
			}
			if (activeWorkers === 0 && jobIndex >= jobs.length) {
				allJobsQueued = true;
				wakeWriter();
			}
		};
		const processJob = async (job, index) => {
			let jobResult = null;
			const target = normalizeName(job.target);
			try {
				if (job.type === "content" || job.type === "stream") {
					let body;
					let size;
					const isDir = target.endsWith("/");
					if (job.type === "stream") {
						if (!isDir && job.size <= 0 || isDir && job.size !== 0) throw new Error(isDir ? "Streams for directories must have size 0." : "Streams require a positive size.");
						size = job.size;
					} else {
						const content = await normalizeBody(job.content);
						size = content.length;
						body = content;
					}
					const stat = {
						size: isDir ? 0 : size,
						isFile: () => !isDir,
						isDirectory: () => isDir,
						isSymbolicLink: () => false,
						mode: job.mode,
						mtime: job.mtime ?? /* @__PURE__ */ new Date(),
						uid: job.uid ?? 0,
						gid: job.gid ?? 0
					};
					if (stream.destroyed) return;
					if (filter && !filter(target, stat)) return;
					if (stream.destroyed) return;
					let header = {
						name: target,
						type: isDir ? DIRECTORY : FILE,
						size: isDir ? 0 : size,
						mode: stat.mode,
						mtime: stat.mtime,
						uid: stat.uid,
						gid: stat.gid,
						uname: job.uname,
						gname: job.gname
					};
					if (map) header = map(header);
					if (stream.destroyed) return;
					if (!isDir && job.type === "stream") {
						body = job.content instanceof Readable ? job.content : Readable.fromWeb(job.content);
						body.once("error", onError);
						bodyStreams.add(body);
					}
					jobResult = {
						header,
						body: isDir ? void 0 : body
					};
					return;
				}
				let source = job.source;
				let stat = await fsp.lstat(source, BIGINT_STAT);
				if (stream.destroyed) return;
				if (dereference && stat.isSymbolicLink()) {
					source = await fsp.realpath(source);
					if (stream.destroyed) return;
					realBaseDir ??= await fsp.realpath(baseDir ?? directoryPath ?? process.cwd());
					if (stream.destroyed) return;
					const relativeToBase = path.relative(realBaseDir, source);
					if (relativeToBase === ".." || relativeToBase.startsWith(".." + path.sep) || path.isAbsolute(relativeToBase)) return;
					stat = await fsp.lstat(source, BIGINT_STAT);
					if (stat.isSymbolicLink()) return;
				}
				if (stream.destroyed) return;
				if (filter && !filter(job.source, stat)) return;
				if (stream.destroyed) return;
				let header = {
					name: target,
					size: 0,
					mode: (job.mode ?? Number(stat.mode)) & 4095,
					mtime: job.mtime === void 0 ? stat.mtime : new Date(job.mtime.getTime()),
					uid: job.uid ?? Number(stat.uid),
					gid: job.gid ?? Number(stat.gid),
					uname: job.uname,
					gname: job.gname,
					type: FILE
				};
				let body;
				let hardlinkId;
				if (stat.isDirectory()) {
					header.type = DIRECTORY;
					header.name = target.endsWith("/") ? target : `${target}/`;
					try {
						const entries = await fsp.readdir(source, WITH_FILE_TYPES);
						if (stream.destroyed) return;
						const after = await fsp.lstat(source, BIGINT_STAT);
						if (stream.destroyed || !after.isDirectory() || stat.dev !== after.dev || stat.ino !== after.ino) return;
						for (const d of entries) jobs.push({
							type: d.isDirectory() ? DIRECTORY : FILE,
							source: path.join(source, d.name),
							target: `${header.name}${d.name}`,
							mtime: job.mtime,
							uid: job.uid,
							gid: job.gid,
							uname: job.uname,
							gname: job.gname,
							mode: job.mode
						});
					} catch (error) {
						const code = error.code;
						if (code !== "ENOENT" && code !== "ENOTDIR") throw error;
					}
				} else if (stat.isSymbolicLink()) {
					header.type = SYMLINK;
					header.linkname = await fsp.readlink(job.source);
				} else if (stat.isFile()) {
					header.size = Number(stat.size);
					let handleToClose;
					if (stat.nlink > 1n && (process.platform !== "win32" || stat.dev !== 0n && stat.ino !== -1n)) hardlinkId = `${stat.dev}:${stat.ino}`;
					try {
						let after;
						try {
							if (header.size === 0) after = await fsp.lstat(source, BIGINT_STAT);
							else {
								handleToClose = await fsp.open(source, fs.constants.O_NOFOLLOW ?? 0);
								fileHandles.set(handleToClose, null);
							}
						} catch (error) {
							const code = error.code;
							if (code === "ELOOP" || code === "ENOENT") return;
							throw error;
						}
						if (stream.destroyed) return;
						if (after) {
							if (!after.isFile() || stat.dev !== after.dev || stat.ino !== after.ino) return;
						} else {
							const { dev, ino } = await handleToClose.stat(BIGINT_STAT);
							if (stream.destroyed) return;
							if (stat.dev !== dev || stat.ino !== ino) return;
						}
						if (header.size > 0) {
							const handle = handleToClose;
							if (header.size < 32768) {
								const buffer = Buffer.allocUnsafe(header.size);
								let offset = 0;
								while (offset < buffer.length && !stream.destroyed) {
									const { bytesRead } = await handle.read(buffer, offset, buffer.length - offset, offset);
									if (bytesRead === 0) break;
									offset += bytesRead;
								}
								body = offset === buffer.length ? buffer : buffer.subarray(0, offset);
							} else {
								body = {
									handle,
									size: header.size
								};
								handleToClose = void 0;
							}
						}
					} finally {
						if (handleToClose) await closeHandle(handleToClose);
					}
				} else return;
				if (stream.destroyed) return;
				if (hardlinkId) jobResult = {
					header,
					body,
					hardlinkId
				};
				else {
					if (map) header = map(header);
					jobResult = {
						header,
						body
					};
				}
			} finally {
				if (stream.destroyed) {
					if (jobResult?.body instanceof Readable) destroyBody(jobResult.body, cancelError);
					else if (jobResult?.body && !(jobResult.body instanceof Uint8Array)) await closeHandle(jobResult.body.handle);
				} else {
					results.set(index, jobResult);
					if (index === writeIndex) wakeWriter();
				}
			}
		};
		controller();
		await writer();
		if (!stream.destroyed) {
			packer.finalize();
			stream.push(null);
		}
	})().catch(onError);
	return stream;
}
//#endregion
//#region src/fs/concurrency.ts
const createOperationQueue = (concurrency) => {
	let active = 0;
	const tasks = [];
	let head = 0;
	let idle = null;
	let resolveIdle = null;
	const ensureIdle = () => idle ??= new Promise((resolve) => resolveIdle = resolve);
	const flush = () => {
		while (active < concurrency && head < tasks.length) {
			const task = tasks[head++];
			active++;
			task().finally(() => {
				active--;
				flush();
			});
		}
		if (head === tasks.length) {
			tasks.length = 0;
			head = 0;
			if (active === 0 && resolveIdle) {
				resolveIdle();
				idle = null;
				resolveIdle = null;
			}
		}
	};
	return {
add(op) {
			const wasIdle = active === 0 && head === tasks.length;
			return new Promise((resolve, reject) => {
				tasks.push(() => Promise.resolve().then(op).then(resolve, reject));
				if (wasIdle) ensureIdle();
				flush();
			});
		},
		onIdle() {
			return active === 0 && head === tasks.length ? Promise.resolve() : ensureIdle();
		}
	};
};
//#endregion
//#region src/fs/file-sink.ts
const BATCH_BYTES = 262144;
const BUFFER_LIMIT = 8388608;
const MAX_WRITE_VECTORS = 1024;
const CREATE_FLAGS = fs.constants.O_WRONLY | fs.constants.O_CREAT | fs.constants.O_TRUNC | (fs.constants.O_NOFOLLOW ?? 0) | fs.constants.O_EXCL;
const STATE_OPENING = 1;
const STATE_OPEN = 2;
const STATE_CLOSED = 3;
const STATE_FAILED = 4;
const DRAINED_PROMISE = Promise.resolve();
const discardFile = (fd) => fs.ftruncate(fd, 0, () => fs.close(fd));
function createFileSink(path, { mode = 438, mtime } = {}, onError) {
	let state = STATE_OPENING;
	let flushing = false;
	let fd = null;
	let queue = [];
	let spare = [];
	let bytes = 0;
	let storedError = null;
	let failedFd = null;
	let endPromise = null;
	let endResolve = null;
	let endReject = null;
	let drainPromise = null;
	let drainResolve = null;
	let drainReject = null;
	const settleDrain = (error) => {
		if (!drainPromise) return;
		const resolve = drainResolve;
		const reject = drainReject;
		drainPromise = null;
		drainResolve = null;
		drainReject = null;
		if (error) reject?.(error);
		else resolve?.();
	};
	const resetBuffers = () => {
		bytes = 0;
		queue.length = 0;
		spare.length = 0;
	};
	const finish = () => {
		if (state === STATE_FAILED) return;
		state = STATE_CLOSED;
		endResolve?.();
		settleDrain();
	};
	const fail = (error) => {
		if (storedError) return;
		storedError = error;
		state = STATE_FAILED;
		const writePending = flushing;
		resetBuffers();
		const fdToClose = fd;
		fd = null;
		if (fdToClose !== null) if (writePending) failedFd = fdToClose;
		else discardFile(fdToClose);
		flushing = false;
		if (endReject) endReject(error);
		else onError?.(error);
		settleDrain(error);
	};
	const close = () => {
		if (fd === null) {
			finish();
			return;
		}
		const fdToClose = fd;
		fd = null;
		if (mtime) fs.futimes(fdToClose, mtime, mtime, (err) => {
			if (state !== STATE_OPEN) {
				fs.close(fdToClose);
				return;
			}
			if (err) {
				fs.close(fdToClose, () => fail(err));
				return;
			}
			fs.close(fdToClose, (closeErr) => {
				if (state !== STATE_OPEN) return;
				if (closeErr) fail(closeErr);
				else finish();
			});
		});
		else fs.close(fdToClose, (err) => {
			if (state !== STATE_OPEN) return;
			if (err) fail(err);
			else finish();
		});
	};
	const flush = () => {
		if (flushing || queue.length === 0 || state !== STATE_OPEN) return;
		flushing = true;
		let bufs = queue;
		queue = spare;
		spare = bufs;
		queue.length = 0;
		let pendingBytes = bytes;
		const onDone = (err, written = 0) => {
			if (state !== STATE_OPEN) {
				if (failedFd !== null) {
					const fdToClose = failedFd;
					failedFd = null;
					discardFile(fdToClose);
				}
				return;
			}
			if (err) {
				flushing = false;
				fail(err);
				return;
			}
			if (written <= 0 || written > pendingBytes) {
				flushing = false;
				fail(/* @__PURE__ */ new Error("File write made no progress."));
				return;
			}
			bytes -= written;
			pendingBytes -= written;
			if (pendingBytes > 0) {
				let skipped = written;
				let index = 0;
				while (skipped >= bufs[index].length) skipped -= bufs[index++].length;
				bufs = bufs.slice(index);
				if (skipped > 0) bufs[0] = bufs[0].subarray(skipped);
				if (bufs.length === 1) {
					const buf = bufs[0];
					fs.write(fd, buf, 0, buf.length, null, onDone);
				} else fs.writev(fd, bufs, onDone);
				return;
			}
			flushing = false;
			spare.length = 0;
			if (bytes < BUFFER_LIMIT) settleDrain();
			if (queue.length > 0) flush();
			else if (endResolve) close();
		};
		if (bufs.length === 1) {
			const buf = bufs[0];
			fs.write(fd, buf, 0, buf.length, null, onDone);
		} else fs.writev(fd, bufs, onDone);
	};
	const onOpen = (err, openFd) => {
		if (err) return fail(err);
		if (state >= STATE_CLOSED) {
			fs.close(openFd);
			return;
		}
		fd = openFd;
		state = STATE_OPEN;
		if (endResolve) if (queue.length > 0) flush();
		else close();
		else if (bytes >= BATCH_BYTES || queue.length >= MAX_WRITE_VECTORS) flush();
		else settleDrain();
	};
	const write = (chunk) => {
		if (state >= STATE_CLOSED || endResolve) return false;
		queue.push(chunk);
		bytes += chunk.length;
		if (state === STATE_OPEN && !flushing && (bytes >= BATCH_BYTES || queue.length >= MAX_WRITE_VECTORS)) flush();
		return bytes < BUFFER_LIMIT && queue.length < MAX_WRITE_VECTORS;
	};
	const waitDrain = () => {
		if (storedError) return Promise.reject(storedError);
		if (state === STATE_OPENING || state === STATE_OPEN && (bytes >= BUFFER_LIMIT || queue.length >= MAX_WRITE_VECTORS)) return drainPromise ??= new Promise((resolve, reject) => {
			drainResolve = resolve;
			drainReject = reject;
		});
		return DRAINED_PROMISE;
	};
	const end = () => {
		if (storedError) return Promise.reject(storedError);
		if (state >= STATE_CLOSED) return DRAINED_PROMISE;
		if (endPromise) return endPromise;
		endPromise = new Promise((resolve, reject) => {
			endResolve = resolve;
			endReject = reject;
			if (state === STATE_OPEN && !flushing) if (queue.length > 0) flush();
			else close();
		});
		return endPromise;
	};
	const destroy = (error) => {
		if (error) {
			fail(error);
			return;
		}
		if (state >= STATE_CLOSED) return;
		resetBuffers();
		flushing = false;
		if (fd !== null) {
			const fdToClose = fd;
			fd = null;
			fs.close(fdToClose);
		}
		finish();
	};
	fs.open(path, CREATE_FLAGS, mode, (err, openFd) => {
		if (err?.code !== "EEXIST") return onOpen(err, openFd);
		if (state !== STATE_OPENING) return;
		fs.rm(path, { force: true }, (rmErr) => {
			if (rmErr) return fail(rmErr);
			if (state !== STATE_OPENING) return;
			fs.open(path, CREATE_FLAGS, mode, onOpen);
		});
	});
	return {
		write,
		end,
		destroy,
		waitDrain
	};
}
//#endregion
//#region src/fs/cache.ts
const createCache = () => {
	const m = /* @__PURE__ */ new Map();
	return {
get(k) {
			const v = m.get(k);
			if (m.delete(k)) m.set(k, v);
			return v;
		},
set(k, v) {
			if (m.set(k, v).size > 1e4) m.delete(m.keys().next().value);
		},
		clear() {
			m.clear();
		}
	};
};
//#endregion
//#region src/fs/path-cache.ts
const ENOENT = "ENOENT";
const MAX_SYMLINKS = 64;
const linkSep = process.platform === "win32" ? /[/\\]/ : "/";
const linkParts = (linkname) => linkname.split(linkSep).filter((part) => part && part !== ".");
const createPathCache = (destDirPath, options, opQueue, concurrency) => {
	const { maxDepth = 1024, dmode } = options;
	const dirPromises = createCache();
	const pathConflicts = /* @__PURE__ */ new Map();
	const deferredLinks = [];
	let symlinks;
	const realDirCache = createCache();
	const initializeDestDir = async (destDirPath) => {
		const symbolic = path.resolve(destDirPath);
		try {
			await fsp.mkdir(symbolic, { recursive: true });
		} catch (err) {
			if (err.code === ENOENT) {
				const parentDir = path.dirname(symbolic);
				if (parentDir === symbolic) throw err;
				await fsp.mkdir(parentDir, { recursive: true });
				await fsp.mkdir(symbolic, { recursive: true });
			} else throw err;
		}
		try {
			return {
				symbolic,
				real: await fsp.realpath(symbolic)
			};
		} catch (err) {
			if (err.code === ENOENT) return {
				symbolic,
				real: symbolic
			};
			throw err;
		}
	};
	const destDirPromise = initializeDestDir(destDirPath);
	destDirPromise.catch(() => {});
	const getRealDir = async (dirPath, errorMessage) => {
		const destDir = await destDirPromise;
		if (dirPath === destDir.symbolic) return destDir.real;
		let promise = realDirCache.get(dirPath);
		if (!promise) {
			promise = fsp.realpath(dirPath).then((realPath) => {
				validateBounds(realPath, destDir.real, errorMessage);
				return realPath;
			});
			realDirCache.set(dirPath, promise);
		}
		return promise;
	};
	const prepareDirectory = async (dirPath, mode) => {
		let promise = dirPromises.get(dirPath);
		if (promise) return promise;
		promise = (async () => {
			if (dirPath === (await destDirPromise).symbolic) return;
			await prepareDirectory(path.dirname(dirPath));
			try {
				const stat = await fsp.lstat(dirPath);
				if (stat.isDirectory()) return;
				if (stat.isSymbolicLink()) try {
					const realPath = await getRealDir(dirPath, `Symlink "${dirPath}" points outside the extraction directory.`);
					if ((await fsp.stat(realPath)).isDirectory()) return;
				} catch (err) {
					if (err.code === ENOENT) throw new Error(`Symlink "${dirPath}" points outside the extraction directory.`);
					throw err;
				}
				throw new Error(`"${dirPath}" is not a valid directory component.`);
			} catch (err) {
				if (err.code === ENOENT) {
					await fsp.mkdir(dirPath, { mode: mode ?? options.dmode });
					return;
				}
				throw err;
			}
		})();
		dirPromises.set(dirPath, promise);
		return promise;
	};
	return {
async ready() {
			await destDirPromise;
		},
async preparePath(header) {
			const { name, linkname, type, mode, mtime } = header;
			const normalizedName = normalizeHeaderName(name);
			const destDir = await destDirPromise;
			const outPath = path.join(destDir.symbolic, normalizedName);
			if (maxDepth !== Infinity) {
				let depth = 1;
				for (const char of normalizedName) if (char === "/" && ++depth > maxDepth) throw new Error("Tar exceeds max specified depth.");
			}
			const prevOp = pathConflicts.get(normalizedName);
			if (prevOp) {
				if (prevOp === "directory" && type !== "directory" || prevOp !== "directory" && type === "directory") throw new Error(`Path conflict ${type} over existing ${prevOp} at "${name}"`);
				return;
			}
			const parentDir = path.dirname(outPath);
			switch (type) {
				case DIRECTORY: {
					pathConflicts.set(normalizedName, DIRECTORY);
					const safeMode = mode === void 0 ? void 0 : mode & 511;
					await prepareDirectory(outPath, dmode ?? safeMode);
					if (mtime) await fsp.lutimes(outPath, mtime, mtime).catch(() => {});
					return;
				}
				case FILE:
					pathConflicts.set(normalizedName, FILE);
					await prepareDirectory(parentDir);
					return path.join(await getRealDir(parentDir, `File "${name}" points outside the extraction directory.`), path.basename(outPath));
				case SYMLINK: {
					pathConflicts.set(normalizedName, SYMLINK);
					if (!linkname) return;
					validateBounds(path.resolve(parentDir, linkname), destDir.symbolic, `Symlink "${linkname}" points outside the extraction directory.`);
					await prepareDirectory(parentDir);
					const realParentDir = await fsp.realpath(parentDir);
					validateBounds(realParentDir, destDir.real, "Symlink parent changed.");
					validateBounds(path.resolve(realParentDir, linkname), destDir.real, `Symlink "${linkname}" points outside the extraction directory.`);
					const realOutPath = path.join(realParentDir, path.basename(outPath));
					try {
						await fsp.symlink(linkname, realOutPath);
					} catch (err) {
						if (err.code !== "EEXIST") throw err;
						await fsp.rm(realOutPath, { force: true });
						if (await fsp.realpath(parentDir) !== realParentDir) throw new Error("Symlink parent changed.");
						await fsp.symlink(linkname, realOutPath);
					}
					(symlinks ??= []).push([normalizedName, linkname]);
					dirPromises.clear();
					realDirCache.clear();
					if (mtime) await fsp.lutimes(outPath, mtime, mtime).catch(() => {});
					return;
				}
				case LINK: {
					pathConflicts.set(normalizedName, LINK);
					if (!linkname) return;
					if (path.isAbsolute(linkname)) throw new Error(`Hardlink "${linkname}" points outside the extraction directory.`);
					const linkTarget = path.join(destDir.symbolic, linkname);
					validateBounds(linkTarget, destDir.symbolic, `Hardlink "${linkname}" points outside the extraction directory.`);
					await prepareDirectory(parentDir);
					if (linkTarget !== outPath) deferredLinks.push({
						linkTarget,
						outPath
					});
					return;
				}
				default: return;
			}
		},
async checkSymlinks() {
			if (!symlinks) return;
			const { symbolic: dest, real } = await destDirPromise;
			const realPrefix = real + path.sep;
			const root = path.parse(real).root;
			const depth = linkParts(real.slice(root.length)).length;
			const targetParts = (linkname, resolvedParts, message) => {
				if (!path.isAbsolute(linkname)) return linkParts(linkname);
				validateBounds(linkname, real, message);
				resolvedParts.length = 0;
				const parts = linkParts(linkname.slice(root.length));
				parts.splice(0, depth);
				return parts;
			};
			const getSymlinkError = async ([name, storedLinkname]) => {
				const outPath = path.join(dest, name);
				try {
					try {
						const resolved = await fsp.realpath(outPath);
						if (resolved !== real && !resolved.startsWith(realPrefix)) throw new Error(`Symlink "${storedLinkname}" points outside the extraction directory.`);
						return;
					} catch (err) {
						if (err.code !== ENOENT) throw err;
					}
					if (!(await fsp.lstat(outPath)).isSymbolicLink()) return;
					const linkname = await fsp.readlink(outPath);
					const message = `Symlink "${linkname}" points outside the extraction directory.`;
					const realParent = await fsp.realpath(path.dirname(outPath));
					validateBounds(realParent, real, message);
					const resolvedParts = linkParts(path.relative(real, realParent));
					const pendingParts = targetParts(linkname, resolvedParts, message);
					let followedSymlinks = 0;
					for (let i = 0; i < pendingParts.length; i++) {
						const part = pendingParts[i];
						if (part === "..") {
							if (!resolvedParts.length) throw new Error(message);
							resolvedParts.pop();
							continue;
						}
						resolvedParts.push(part);
						const nextPath = path.join(real, ...resolvedParts);
						let nextStat;
						try {
							nextStat = await fsp.lstat(nextPath);
						} catch (err) {
							if (err.code === ENOENT) continue;
							throw err;
						}
						if (!nextStat.isSymbolicLink()) continue;
						if (++followedSymlinks > MAX_SYMLINKS) throw new Error(message);
						const nextLink = await fsp.readlink(nextPath);
						resolvedParts.pop();
						pendingParts.splice(i + 1, 0, ...targetParts(nextLink, resolvedParts, message));
					}
				} catch (err) {
					if (err.code !== ENOENT) return err;
				}
			};
			for (let start = 0; start < symlinks.length; start += concurrency) {
				const batch = symlinks.slice(start, start + concurrency);
				const errors = await Promise.all(batch.map((symlink) => opQueue.add(() => getSymlinkError(symlink))));
				for (const [i, error] of errors.entries()) {
					if (error === void 0) continue;
					await fsp.rm(path.join(dest, batch[i][0]), { force: true });
					throw error;
				}
			}
		},
async applyLinks() {
			const destRoot = (await destDirPromise).real;
			for (const { linkTarget, outPath } of deferredLinks) try {
				const realTargetDir = await fsp.realpath(path.dirname(linkTarget));
				validateBounds(realTargetDir, destRoot, `Hardlink "${linkTarget}" points outside the extraction directory.`);
				const realTarget = path.join(realTargetDir, path.basename(linkTarget));
				const [targetResult, outDirResult] = await Promise.allSettled([opQueue.add(() => fsp.lstat(realTarget)), opQueue.add(() => fsp.realpath(path.dirname(outPath)))]);
				if (targetResult.status === "rejected") throw targetResult.reason;
				const targetStat = targetResult.value;
				if (targetStat.isSymbolicLink()) throw new Error(`Hardlink "${linkTarget}" is a symlink.`);
				if (outDirResult.status === "rejected") throw outDirResult.reason;
				const realOutDir = outDirResult.value;
				validateBounds(realOutDir, destRoot, `Hardlink "${outPath}" points outside the extraction directory.`);
				const realOutPath = path.join(realOutDir, path.basename(outPath));
				try {
					await fsp.link(realTarget, realOutPath);
				} catch (err) {
					const code = err.code;
					if (code !== "EEXIST" && code !== ENOENT) throw err;
					try {
						const outStat = await fsp.lstat(realOutPath);
						if (outStat.dev === targetStat.dev && outStat.ino === targetStat.ino) continue;
						await fsp.rm(realOutPath, { force: true });
					} catch (err) {
						if (err.code !== ENOENT) throw err;
					}
					await fsp.link(realTarget, realOutPath);
				}
				const linkStat = await fsp.lstat(realOutPath);
				if (linkStat.dev !== targetStat.dev || linkStat.ino !== targetStat.ino) {
					await fsp.rm(realOutPath, { force: true });
					throw new Error(`Hardlink target "${linkTarget}" changed during creation for link at "${outPath}".`);
				}
			} catch (err) {
				if (err.code === ENOENT) throw new Error(`Hardlink target "${linkTarget}" does not exist for link at "${outPath}".`);
				throw err;
			}
		}
	};
};
//#endregion
//#region src/fs/unpack.ts
function unpackTar(directoryPath, options = {}) {
	const unpacker = createUnpacker(options);
	const concurrency = options.concurrency || cpus().length || 8;
	const opQueue = createOperationQueue(concurrency);
	let cancelError;
	const pathCache = createPathCache(directoryPath, options, opQueue, concurrency);
	let currentFileStream = null;
	const fileStreams = /* @__PURE__ */ new Set();
	let needsDrain = false;
	const writeCurrent = (chunk) => {
		const writeOk = currentFileStream.write(chunk);
		if (!writeOk) needsDrain = true;
		return writeOk;
	};
	const onFileError = (err) => {
		if (!writable.destroyed) writable.destroy(err);
	};
	const closeCurrent = () => {
		const stream = currentFileStream;
		currentFileStream = null;
		opQueue.add(() => stream.end()).then(() => fileStreams.delete(stream), (err) => {
			fileStreams.delete(stream);
			onFileError(err);
		});
	};
	const writable = new Writable({
		async write(chunk, _, cb) {
			let pendingFileOpens;
			let writeError;
			try {
				unpacker.write(chunk);
				if (unpacker.isEntryActive()) {
					if (currentFileStream) {
						while (!unpacker.isBodyComplete()) {
							needsDrain = false;
							const fed = unpacker.streamBody(writeCurrent);
							if (needsDrain) await currentFileStream.waitDrain();
							else if (fed === 0) return;
						}
						if (!unpacker.skipPadding()) return;
						closeCurrent();
					} else if (!unpacker.skipEntry()) return;
				}
				while (true) {
					const header = unpacker.readHeader();
					if (header === void 0 || header === null) return;
					const transformedHeader = transformHeader(header, options);
					if (!transformedHeader) {
						if (!unpacker.skipEntry()) return;
						continue;
					}
					const outPath = await opQueue.add(() => pathCache.preparePath(transformedHeader));
					if (cancelError) throw cancelError;
					if (outPath) {
						const safeMode = transformedHeader.mode === void 0 ? void 0 : transformedHeader.mode & 511;
						currentFileStream = createFileSink(outPath, {
							mode: options.fmode ?? safeMode,
							mtime: transformedHeader.mtime ?? void 0
						}, onFileError);
						fileStreams.add(currentFileStream);
						(pendingFileOpens ??= []).push(currentFileStream.waitDrain().catch((error) => error));
						while (!unpacker.isBodyComplete()) {
							needsDrain = false;
							const fed = unpacker.streamBody(writeCurrent);
							if (needsDrain) await currentFileStream.waitDrain();
							else if (fed === 0) return;
						}
						if (!unpacker.skipPadding()) return;
						closeCurrent();
					} else if (!unpacker.skipEntry()) return;
				}
			} catch (err) {
				writeError = err;
			} finally {
				const openError = pendingFileOpens ? (await Promise.all(pendingFileOpens)).find((error) => error) : void 0;
				cb(cancelError ?? openError ?? writeError);
			}
		},
		async final(cb) {
			try {
				unpacker.end();
				unpacker.validateEOF();
				if (currentFileStream) closeCurrent();
				await pathCache.ready();
				await opQueue.onIdle();
				if (cancelError) throw cancelError;
				await pathCache.checkSymlinks();
				await pathCache.applyLinks();
				cb();
			} catch (err) {
				cb(err);
			}
		},
		destroy(error, callback) {
			const hasWork = fileStreams.size > 0 || writable.writableLength > 0 || writable.writableEnded && !writable.writableFinished;
			if (!error && !hasWork) {
				callback(null);
				return;
			}
			cancelError = error ?? AbortSignal.abort().reason;
			for (const stream of fileStreams) stream.destroy(cancelError);
			fileStreams.clear();
			currentFileStream = null;
			callback(cancelError);
		}
	});
	return writable;
}
//#endregion
export { packTar, packTarSources, unpackTar };
