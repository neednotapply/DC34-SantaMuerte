/* The strip of tape on a note is its delete control.
 *
 * Nothing on the wall advertises this: the boards stay as uncluttered as a
 * corridor of paper, and the only affordance is that the tape takes a cursor
 * and answers to a keyboard. The tape used to be a ::before pseudo-element,
 * which can be painted but never clicked or focused, so each board now builds
 * a real button and the pseudo-element is gone.
 *
 * Shared by Field Notes, the NFC log and both script shelves so that one
 * gesture means the same thing everywhere.
 */
(function () {
  const T = value => (window.PortalLocale ? PortalLocale.convert(value, PortalLocale.locale) : value);

  let overlay = null;
  let dismiss = null;

  function build() {
    overlay = document.createElement('div');
    overlay.className = 'ask-overlay';
    overlay.hidden = true;
    overlay.innerHTML =
      '<div class="ask" role="dialog" aria-modal="true" aria-labelledby="askText">' +
      '<p class="ask-text" id="askText"></p>' +
      '<div class="ask-row">' +
      '<button type="button" class="ask-yes"></button>' +
      '<button type="button" class="ask-no"></button>' +
      '</div></div>';
    document.body.appendChild(overlay);
    // A click on the paper itself must not count as an answer, but one on the
    // dark around it is the same as saying no.
    overlay.addEventListener('click', event => {
      if (event.target === overlay && dismiss) dismiss(false);
    });
    document.addEventListener('keydown', event => {
      if (event.key === 'Escape' && dismiss) dismiss(false);
    });
  }

  // Resolves true only when someone actually chooses yes.
  function ask(question) {
    if (!overlay) build();
    const text = overlay.querySelector('.ask-text');
    const yes = overlay.querySelector('.ask-yes');
    const no = overlay.querySelector('.ask-no');
    text.textContent = T(question);
    yes.textContent = T('Yes');
    no.textContent = T('No');

    // A dialog already up means its caller is still awaiting an answer.
    // Leaving it pending stranded that note's tape disabled forever.
    if (dismiss) dismiss(false);

    const previous = document.activeElement;
    overlay.hidden = false;
    // No is focused rather than Yes: the dialog exists to slow a delete down,
    // so a stray Return should cancel it, not confirm it.
    no.focus();

    return new Promise(resolve => {
      dismiss = answer => {
        if (!dismiss) return;
        dismiss = null;
        yes.onclick = no.onclick = null;
        overlay.hidden = true;
        if (previous && previous.isConnected) previous.focus();
        resolve(answer);
      };
      yes.onclick = () => dismiss(true);
      no.onclick = () => dismiss(false);
    });
  }

  /* Give one note its tape. `label` is what a screen reader announces, and
   * `onDelete` runs only after a yes; returning false from it leaves the note
   * in place, so a board can report a failed request without losing the note.
   */
  function tape(note, options) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'tape';
    const label = (options && options.label) || 'Remove note';
    const localizedLabel = T(label);
    button.setAttribute('aria-label', localizedLabel);
    button.title = localizedLabel;
    button.addEventListener('click', async event => {
      event.preventDefault();
      event.stopPropagation();
      if (button.disabled) return;
      const sure = await ask((options && options.question) || `${label}?`);
      if (!sure) return;
      button.disabled = true;
      // The badge can take a second to answer, which left the note sitting
      // there looking as though nothing had happened. Fading it while the
      // request is in flight says "working on it" without claiming the note
      // is gone -- if the delete is refused it simply comes back.
      note.classList.add('note-pending');
      let done = true;
      try {
        done = (await options.onDelete()) !== false;
      } catch (_) {
        done = false;
      }
      note.classList.remove('note-pending');
      if (done) {
        note.classList.add('note-going');
        // Long enough for the note to lift off the wall, short enough that a
        // second delete does not feel queued behind it.
        window.setTimeout(() => note.remove(), 220);
      } else {
        button.disabled = false;
      }
    });
    note.insertBefore(button, note.firstChild);
    return button;
  }

  window.PortalTape = { ask, tape };
})();
