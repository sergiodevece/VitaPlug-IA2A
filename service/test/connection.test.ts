import {test} from 'node:test';
import assert from 'node:assert/strict';
import {existsSync, mkdtempSync, readFileSync, rmSync, statSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {publishLocalConnection} from '../src/connection.ts';

test('the running bridge publishes only a private loopback connection and removes its own descriptor', t => {
  const directory=mkdtempSync(join(tmpdir(),'ia2a-connection-'));
  t.after(()=>rmSync(directory,{recursive:true,force:true}));
  const path=join(directory,'connection.json');
  const cleanPrevious=publishLocalConnection(48220,'1'.repeat(64),directory);
  assert.equal(statSync(path).mode & 0o777,0o600);
  assert.deepEqual(JSON.parse(readFileSync(path,'utf8')),{port:48220,token:'1'.repeat(64)});
  const cleanCurrent=publishLocalConnection(48221,'2'.repeat(64),directory);
  cleanPrevious();
  assert.equal(JSON.parse(readFileSync(path,'utf8')).port,48221);
  cleanCurrent();
  cleanCurrent();
  assert.equal(existsSync(path),false);
});
