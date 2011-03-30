/**
 * @addtogroup TaoChooser Command chooser
 * @ingroup TaoBuiltins
 *
 * Create and display a command menu with interactive search capabilities.
 *
 * The command chooser primitives are designed to enable the creation of
 * interactive menus within a presentation. The display of the menu may be
 * triggered by a key press or mouse click, for instance. When the chooser
 * is active, the user can:
 *   @li use the up and down arrow keys to scroll through the menu options,
 *   @li press return to select an entry and execute the associated action,
 *   @li press escape to leave the menu
 *   @li use the keyboard to perform an interactive search among the chooser
 *       options. Following each key press, the list of entries is reduced
 *       to show only the ones that match the text the user has entered so
 *       far. For instance, type "e": the chooser will show only the options
 *       that contain an "e". Press "x": only the entry that contain "ex"
 *       will be displayed.
 *
 * @note You cannot validate an entry with the mouse. You need to press the
 * return key instead.
 *
 * The same primitives are used by Tao to display the main application
 * menu (when you press the escape key).
 *
 * Let's illustrate this with an example,
 * (<a href="examples/chooser.ddd">chooser.ddd</a>).
 * @include chooser.ddd
 *
 * The first picture shows the command chooser as the user has just pressed
 * the "a" key to show the manu. The second one show the updated command list
 * after the user entered a "g".
 * @image html chooser.png "Using the command chooser"
 *
 * @note The message sent by writeln are visible on the application's
 * standard ouput. See @ref secStdoutStderr "Standard Ouput, Standard Error"
 * for details.
 * @note Currently, the XL forms controlling the appearance of the chooser
 * are defined in <tt>tao.xl</tt> and cannot be overriden in a Tao document.
 *
 * @{
 */


/**
 * Creates and shows a chooser with the given caption.
 * A chooser shows a selection among the possible commands.
 */
tree chooser(caption:text);


/**
 * Adds a command into the current chooser.
 * Creates a chooser item, associates the @p action code block to the item,
 * and appends a new line to the current chooser list. @p action is
 * executed when the user presses the return key on the highlighted command.
 */
tree chooser_choice(label:text, action:tree);


/**
 * Adds commands into the current chooser, by name.
 * All the commands in the current symbol table that have the given prefix are
 * added to the chooser.
 * The current symbol table is scanned, and each time a parameterless function
 * starting with @p prefix is found, a new command is added to the current
 * chooser. The label of the command is the concatenation of:
 *    @li @p label_prefix, and
 *    @li the end of the symbol name, with all underscores changed into spaces.
 *
 * When a command is chosen, the associated action is executed.
 */
tree chooser_commands(prefix:text, label_prefix:text);

/**
 * Adds one command for each page into the current chooser.
 * This primitive is useful when you want to give the user the opportunity to
 * select a page from the current document, and execute an action related to
 * this page. The page table is scanned, and for each page a new command is added
 * into the current chooser. The label of the command is the concatenation of:
 *    @li @p label_prefix,
 *    @li the page number,
 *    @li a space character, and
 *    @li the page name.
 *
 * When a command is chosen, the symbol action is executed and is passed the
 * page name (without the page number).
 */
tree chooser_pages(action:name, label_prefix:text);

/**
 * @}
 */
