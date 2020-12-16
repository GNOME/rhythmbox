
var timer = null;
var lasttime = null;
var seeking = false;
var lastposition = 0.0;

var streaming = false;
var needsync = false;
var audiotag = null;
var s = null;

var accesskey = '';

var sign = function(path) {
	sh = new SipHash();
	ts = new Date().getTime();
	message = path + "\n" + ts;
	return "sig=" + sh.hash_hex(sh.string_to_key(accesskey), message) + "&ts=" + ts;
};

var pad = function(str, len, chr, left) {
	var padding = (str.length >= len) ? '' : new Array(1 + len - str.length >>> 0).join(chr);
	return left ? str + padding : padding + str;
};
var timestr = function(s) {
	var h = Math.floor(s / (60*60));
	var m = Math.floor((s % (60*60)) / 60);
	var sec = Math.floor(s % 60);
	if (h >= 1) {
		return pad(h.toString(), 2, '0', false) + ":" +
			pad(m.toString(), 2, '0', false) + ":" +
			pad(sec.toString(), 2, '0', false);
	} else {
		return pad(m.toString(), 2, '0', false) + ":" +
			pad(sec.toString(), 2, '0', false);
	}
};

var tick = function() {
	now = new Date();
	var elapsed = (now.getTime() - lasttime.getTime());
	lasttime = now;

	if (seeking === false) {
		lastposition = lastposition + elapsed;
		document.getElementById("seekbar-range").value = lastposition;
		document.getElementById("trackposition").textContent = timestr(lastposition/1000);
	}

	if (streaming && needsync) {
		audioTimeSync();
	}
};

var audioCanSeekTo = function(seektime) {
	for (i = 0; i < audiotag.seekable.length; i++) {
		if (audiotag.seekable.start(i) > seektime) {
			return false;
		}
		if (audiotag.seekable.end(i) > seektime) {
			return true;
		}
	}
	return false;
};

var audioTimeSync = function() {
	if (needsync) {
		if (audioCanSeekTo(lastposition/1000)) {
			// probably check seekable ranges and stuff?
			audiotag.currentTime = lastposition/1000;

			if (timer != null) {
				audiotag.play();
			}
			needsync = false;
		}
	}
};

var createAudioTag = function() {
	audiotag = document.createElement("audio");
	// plugin should give us direct stream urls if they exist
	// and should tell us if the thing can't be streamed (cdda etc.) too
	path = "/entry/current/stream";
	audiotag.setAttribute("src", path + "?" + sign(path));
	audiotag.setAttribute("preload", "auto");

	audiotag.addEventListener('canplay', audioTimeSync);
	needsync = true;

	c = document.getElementById("stream-container");
	while (c.firstChild) {
		c.removeChild(c.firstChild);
	}
	c.appendChild(audiotag);
};

var replaceImage = function(element, imgclass, imgsrc) {
	img = document.createElement("img");
	img.setAttribute("class", imgclass);

	img.setAttribute("src", imgsrc);

	e = document.getElementById(element);
	while (e.firstChild) {
		e.removeChild(e.firstChild);
	}
	e.appendChild(img);
};

var connectionState = function(connected) {
	document.getElementById("connectform").hidden = connected;
	document.getElementById("trackbits").hidden = !connected;
	document.getElementById("seekbar").hidden = !connected;
	pb = document.getElementById("playerbuttons");
	if (connected) {
		pb.classList.remove("pure-button-disabled");
	} else {
		pb.classList.add("pure-button-disabled");
	}
}


var connect = function() {
	accesskey = document.getElementById("accesskey").value;

	loc = document.location.hostname + ":" + document.location.port;
	s = new WebSocket("ws://" + loc + "/ws/player:" + sign("/ws/player"));
	s.onopen = function(event) {
		connectionState(true);
		msg = { "action": "status" };
		s.send(JSON.stringify(msg));
	};

	s.onclose = function(event) {
		document.getElementById("connectionhostname").textContent = "";
		window.clearInterval(timer);
		timer = null;
		lasttime = null;

		connectionState(false);
	};
	s.onmessage = function(event) {
		m = JSON.parse(event.data);
		if ("shutdown" in m) {
			if (streaming) {
				toggleStreaming();
			}
		}
		if ("hostname" in m) {
			document.getElementById("connectionhostname").textContent = m['hostname'];
		}
		if ("id" in m) {
			lastposition = 0;
			document.getElementById("seekbar-range").value = 0;
			document.getElementById("trackposition").textContent = timestr(0);

			if (streaming) {
				createAudioTag();
			}
		}
		if ("title" in m) {
			document.title = m.title;
			document.getElementById("tracktitle").textContent = m.title;
		}
		if ("artist" in m) {
			document.getElementById("trackartist").textContent = m.artist;
		}
		if ("album" in m) {
			document.getElementById("trackalbum").textContent = m.album;
		}
		if ("duration" in m) {
			range = document.getElementById("seekbar-range");
			range.max = m.duration*1000;
			range.min = 0;
			document.getElementById('trackduration').textContent = timestr(m.duration);
		}
		if ("playing" in m) {
			if (m.playing) {
				iconname = "media-playback-pause-symbolic";
				if (timer == null) {
					timer = window.setInterval(tick, 250);
					lasttime = new Date();
				}
				if (streaming) {
					audiotag.play();
				}
			} else {
				iconname = "media-playback-start-symbolic";
				window.clearInterval(timer);
				timer = null;
				lasttime = null;
				if (streaming) {
					audiotag.pause();
				}
			}

			replaceImage("playpause", "", "/icon/" + iconname + "/48");
		}
		if ("position" in m) {
			lastposition = m.position;
			document.getElementById("seekbar-range").value = m.position;
			document.getElementById('trackposition').textContent = timestr(m.position / 1000);
			seeking = false;

			if (streaming) {
				needsync = true;
				audioTimeSync();
			}
		}
		if ("albumart" in m) {
			if (m.albumart != null) {
				path = "/art/" + m.albumart;
				imgsrc = path + "?" + sign(path);
				imgclass = "albumart";
			} else {
				imgsrc = "/icon/org.gnome.Rhythmbox3-symbolic/128";
				imgclass = "noalbumart";
			}
			replaceImage("trackimage", imgclass, imgsrc);
		}
	};

	return false;
};

var sendevent = function() {
	s.send(JSON.stringify({ 'action': this.id }));
};

var seekinput = function() {
	if (seeking === false) {
		time = this.value / 1000;
		s.send(JSON.stringify({ 'action': 'seek', 'time': time}));
		seeking = true;
	}
};

var toggleStreaming = function() {
	if (streaming) {
		streaming = false;
		audiotag.pause();
		c = document.getElementById("stream-container");
		c.removeChild(audiotag);
		audiotag = null;
	} else {
		streaming = true;
		createAudioTag();
	}
};

window.onload = function() {
	document.getElementById("previous").addEventListener('click', sendevent);
	document.getElementById("playpause").addEventListener('click', sendevent);
	document.getElementById("next").addEventListener('click', sendevent);

	document.getElementById("seekbar-range").addEventListener('input', seekinput);

	// document.getElementById("stream-check").addEventListener('click', toggleStreaming);

	document.getElementById("connectform").onsubmit = connect;

	connectionState(false);
}

// siphash implementation from https://github.com/jedisct1/siphash-js

function SipHash() {
    function _add(a, b) {
        var rl = a.l + b.l,
            a2 = { h: a.h + b.h + (rl / 2 >>> 31) >>> 0,
                   l: rl >>> 0 };
        a.h = a2.h; a.l = a2.l;
    }

    function _xor(a, b) {
        a.h ^= b.h; a.h >>>= 0;
        a.l ^= b.l; a.l >>>= 0;
    }

    function _rotl(a, n) {
        var a2 = {
            h: a.h << n | a.l >>> (32 - n),
            l: a.l << n | a.h >>> (32 - n)
        };
        a.h = a2.h; a.l = a2.l;
    }

    function _rotl32(a) {
        var al = a.l;
        a.l = a.h; a.h = al;
    }

    function _compress(v0, v1, v2, v3) {
        _add(v0, v1);
        _add(v2, v3);
        _rotl(v1, 13);
        _rotl(v3, 16);
        _xor(v1, v0);
        _xor(v3, v2);
        _rotl32(v0);
        _add(v2, v1);
        _add(v0, v3);
        _rotl(v1, 17);
        _rotl(v3, 21);
        _xor(v1, v2);
        _xor(v3, v0);
        _rotl32(v2);
    }

    function _get_int(a, offset) {
        return a.charCodeAt(offset + 3) << 24 |
               a.charCodeAt(offset + 2) << 16 |
               a.charCodeAt(offset + 1) << 8 |
               a.charCodeAt(offset);
    }

    function hash(key, m) {
        var k0 = { h: key[1] >>> 0, l: key[0] >>> 0 },
            k1 = { h: key[3] >>> 0, l: key[2] >>> 0 },
            v0 = { h: k0.h, l: k0.l }, v2 = k0,
            v1 = { h: k1.h, l: k1.l }, v3 = k1,
            mi, mp = 0, ml = m.length, ml7 = ml - 7,
            buf = new Uint8Array(new ArrayBuffer(8));

        _xor(v0, { h: 0x736f6d65, l: 0x70736575 });
        _xor(v1, { h: 0x646f7261, l: 0x6e646f6d });
        _xor(v2, { h: 0x6c796765, l: 0x6e657261 });
        _xor(v3, { h: 0x74656462, l: 0x79746573 });
        while (mp < ml7) {
            mi = { h: _get_int(m, mp + 4), l: _get_int(m, mp) };
            _xor(v3, mi);
            _compress(v0, v1, v2, v3);
            _compress(v0, v1, v2, v3);
            _xor(v0, mi);
            mp += 8;
        }
        buf[7] = ml;
        var ic = 0;
        while (mp < ml) {
            buf[ic++] = m.charCodeAt(mp++);
        }
        while (ic < 7) {
            buf[ic++] = 0;
        }
        mi = { h: buf[7] << 24 | buf[6] << 16 | buf[5] << 8 | buf[4],
               l: buf[3] << 24 | buf[2] << 16 | buf[1] << 8 | buf[0] };
        _xor(v3, mi);
        _compress(v0, v1, v2, v3);
        _compress(v0, v1, v2, v3);
        _xor(v0, mi);
        _xor(v2, { h: 0, l: 0xff });
        _compress(v0, v1, v2, v3);
        _compress(v0, v1, v2, v3);
        _compress(v0, v1, v2, v3);
        _compress(v0, v1, v2, v3);

        var h = v0;
        _xor(h, v1);
        _xor(h, v2);
        _xor(h, v3);

        return h;
    }

    function string_to_key(a) {
	var k = [0, 0, 0, 0];
	var i = 0, ki = 0;

	pa = a + "\0\0\0\0";
	while (i < a.length) {
	    k[ki] = (k[ki] + _get_int(pa, i)) % 0xffffffff;
	    i += 4;
	    ki = (ki + 1) % 4;
	}

	return k;
    }

    function hash_hex(key, m) {
        var r = hash(key, m);
        return ("0000000" + r.h.toString(16)).substr(-8) +
               ("0000000" + r.l.toString(16)).substr(-8);
    }

    function hash_uint(key, m) {
        var r = hash(key, m);
        return (r.h & 0x1fffff) * 0x100000000 + r.l;
    }

    return {
        string_to_key: string_to_key,
        hash: hash,
        hash_hex: hash_hex,
        hash_uint: hash_uint
    };
};

