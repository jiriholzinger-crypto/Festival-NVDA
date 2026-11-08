;Copy this file into the festival\lib directory.
;This code helps to implement
;the Festival synthDriver for NVDA screen reader.

;Copyright 2008 Olga Yakovleva <Yakovleva.O.V@gmail.com>

;This program is free software:
;you can redistribute it and/or modify it under the terms of
;the GNU General Public License as published by the Free Software Foundation,
;either version 2 of the License, or (at your option) any later version.
;This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
;without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
;See the GNU General Public License for more details.

;You should have received a copy of the GNU General Public License
;along with this program. If not, see <http://www.gnu.org/licenses/>.

(require 'util)
(require 'tokenize)
(require 'prosody-param)
(require 'voice-select)
(require 'nopauses)

(set! inhibit-initial-pauses t)

(define (set-voice-rate r)
 (set-rate (/ (max 5 (min r 100)) 50)))

(define (get-voice-rate)
 (* (max 0.1 (min 2 (prosody-get-rate))) 50))

(set! voice_us_awb_multisyn_configure_pre nil)
(set! voice_jmk_multisyn_configure_pre nil)

(set! text_to_speak nil)

(define (set-synth-text txt)
 (set! text_to_speak txt))

(define (clear-synth-text)
 (set! text_to_speak nil))

(define (synth-next-part)
 (if text_to_speak
  (let ((result (next-chunk text_to_speak)))
   (let ((utt (car result)) (txt (cadr result)))
    (if (equal? txt "")
	 (set! text_to_speak nil)
	 (set! text_to_speak txt))
    (unwind-protect (utt.wave (utt.synth utt)) 'error)))
 nil))

(provide 'festlib_helper)