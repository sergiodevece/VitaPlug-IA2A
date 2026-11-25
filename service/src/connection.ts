import {mkdirSync, readFileSync, renameSync, unlinkSync, writeFileSync} from 'node:fs';
import {homedir} from 'node:os';
import {join} from 'node:path';

export function publishLocalConnection(port: number, token: string,
    directory = join(homedir(), 'Library', 'Application Support', 'VitaPlug', 'IA2A')) {
  mkdirSync(directory, {recursive:true, mode:0o700});
  const filename = join(directory, 'connection.json');
  const staging = join(directory, 'connection.' + process.pid + '.tmp');
  const contents = JSON.stringify({port, token}) + '\n';
  writeFileSync(staging, contents, {mode:0o600, flag:'wx'});
  try {
    renameSync(staging, filename);
  } catch (error) {
    unlinkSync(staging);
    throw error;
  }
  return () => {
    try {
      if (readFileSync(filename, 'utf8') === contents)
        unlinkSync(filename);
    } catch (error) {
      if (error.code !== 'ENOENT') throw error;
    }
  };
}
