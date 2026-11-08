;Copy this file into the festival\lib directory.
;This code helps to implement
;the Festival synthDriver for NVDA screen reader.

;Copyright 2008 Olga Yakovleva <Yakovleva.O.V@gmail.com>
;Pitch support 2026 Jiri Holzinger

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

;;; Rate
;;; 0..100, 50 = the voice's own tempo (some voices, e.g. kal_diphone and
;;; ked_diphone, set their own Duration_Stretch), 0 = half speed,
;;; 100 = double speed, exponential in between.

(defvar festlib-rate 50)
(defvar festlib-base-stretch (Param.get 'Duration_Stretch))
(if (not (number? festlib-base-stretch))
 (set! festlib-base-stretch 1))

(define (festlib-rate-factor)
 (pow 2 (/ (- festlib-rate 50) 50)))

(define (set-voice-rate r)
 (set! festlib-rate (max 0 (min 100 r)))
 (Param.set 'Duration_Stretch (/ festlib-base-stretch (festlib-rate-factor)))
 festlib-rate)

(define (get-voice-rate)
 festlib-rate)

;; Select a voice and remember its own Duration_Stretch, so that rate 50
;; means the voice's natural tempo.
(define (festlib-select-voice name)
 (Param.set 'Duration_Stretch 1)
 (voice.select name)
 (set! festlib-base-stretch (Param.get 'Duration_Stretch))
 (if (not (number? festlib-base-stretch))
  (set! festlib-base-stretch 1))
 (set-voice-rate festlib-rate))

;;; Pitch
;;; festlib-pitch is 0..100, 50 means the voice's own pitch.
;;; The range is +-12 semitones (one octave down at 0, one octave up at 100).
;;; It works for every voice independently of the intonation method:
;;; the F0 targets of the utterance are scaled just before waveform
;;; synthesis (diphone/UniSyn, MultiSyn, clunits voices) and HTS voices
;;; get the hts_engine "-fm" half-tone shift.

(defvar festlib-pitch 50)

(define (festlib-pitch-semitones)
 (/ (* (- festlib-pitch 50) 12) 50))

(define (festlib-pitch-factor)
 (pow 2 (/ (festlib-pitch-semitones) 12)))

(define (set-voice-pitch p)
 (set! festlib-pitch (max 0 (min 100 p)))
 festlib-pitch)

(define (get-voice-pitch)
 festlib-pitch)

(define (festlib-remove-param name params)
 (cond
  ((null? params) nil)
  ((and (pair? (car params)) (equal? (car (car params)) name))
   (festlib-remove-param name (cdr params)))
  (t (cons (car params) (festlib-remove-param name (cdr params))))))

(define (festlib-apply-pitch utt)
 (let ((factor (festlib-pitch-factor)))
  (if (and (not (equal? factor 1))
           (member (quote Target) (utt.relationnames utt)))
   (mapcar
    (lambda (tgt)
     (item.set_feat tgt "f0" (* factor (item.feat tgt "f0"))))
    (utt.relation.items utt 'Target)))
  (if (symbol-bound? 'hts_engine_params)
   (set! hts_engine_params
    (cons (list "-fm" (festlib-pitch-semitones))
          (festlib-remove-param "-fm" hts_engine_params))))
  utt))

;; Wrap Wave_Synth once, so the pitch is applied for every utterance type
;; and every voice (voices may replace after_analysis_hooks on selection).
(if (not (symbol-bound? 'festlib-orig-Wave_Synth))
 (set! festlib-orig-Wave_Synth Wave_Synth))

(define (Wave_Synth utt)
 (festlib-apply-pitch utt)
 (festlib-orig-Wave_Synth utt))

;;; Additional voices (downloaded by the NVDA add-on) live outside the
;;; add-on, in DIR/<language>/<voice>/, so they survive add-on updates.
;;; The directory comes from the FESTLIB_VOICE_PATH environment variable.
;;; A voice already found in festival/lib/voices is not added again.

(define (festlib-add-voice-dir dir)
 (let ((languages (directory-entries dir t)) language voices voicedir name)
  (while languages
   (set! language (car languages))
   (set! voices (directory-entries (path-append dir language) t))
   (while voices
    (set! voicedir (car voices))
    (set! name (path-basename voicedir))
    (if (and (not (string-matches voicedir ".*\\..*"))
             (not (assoc (intern name) voice-locations))
             (probe_file (path-append dir language voicedir "festvox" (string-append name ".scm"))))
     (voice-location name (path-as-directory (path-append dir language voicedir)) "downloaded voice"))
    (set! voices (cdr voices)))
   (set! languages (cdr languages)))))

(let ((dir (getenv "FESTLIB_VOICE_PATH")))
 (if (and dir (not (equal? dir "")) (probe_file dir))
  (unwind-protect
   (begin
    (set! voice-path (append voice-path (list (path-as-directory dir))))
    (festlib-add-voice-dir (path-as-directory dir)))
   (format t "festlib: cannot read voice directory %s\n" dir))))

(defvar festlib-dict-dir nil)

;;; Downloaded lexicons (FESTLIB_DICT_PATH/oald/...): the OALD lexicon is
;;; not part of the add-on (it is free for non-commercial use only).
(let ((dir (getenv "FESTLIB_DICT_PATH")))
 (if (and dir (not (equal? dir ""))
          (not (probe_file (path-append lexdir "oald/oaldlex.scm")))
          (probe_file (path-append dir "oald/oaldlex.scm")))
  (begin
   (set! festlib-dict-dir (path-as-directory dir))
   (set! setup_oald_lex
    (lambda ()
     (if (not (member_string "oald" (lex.list)))
      ;; oaldlex.scm finds its files through lexdir
      (let ((saved lexdir))
       (set! lexdir festlib-dict-dir)
       (unwind-protect (load (path-append festlib-dict-dir "oald/oaldlex.scm")) nil)
       (set! lexdir saved))))))))

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
